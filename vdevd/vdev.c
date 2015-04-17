/*
   vdev: a virtual device manager for *nix
   Copyright (C) 2014  Jude Nelson

   This program is dual-licensed: you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 3 or later as 
   published by the Free Software Foundation. For the terms of this 
   license, see LICENSE.GPLv3+ or <http://www.gnu.org/licenses/>.

   You are free to use this program under the terms of the GNU General
   Public License, but WITHOUT ANY WARRANTY; without even the implied 
   warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
   See the GNU General Public License for more details.

   Alternatively, you are free to use this program under the terms of the 
   Internet Software Consortium License, but WITHOUT ANY WARRANTY; without
   even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
   For the terms of this license, see LICENSE.ISC or 
   <http://www.isc.org/downloads/software-support-policy/isc-license/>.
*/

#include "vdev.h"
#include "action.h"
#include "libvdev/config.h"

SGLIB_DEFINE_VECTOR_FUNCTIONS( cstr );


// context for removing unplugged device 
struct vdev_device_unplug_context {
   
   struct sglib_cstr_vector* device_paths;      // queue of device paths to search 
   struct vdev_state* state;                    // vdev state
};

// get the instance of the vdevd program that made this device, given its device path 
// instance_str must be at least VDEV_CONFIG_INSTANCE_NONCE_STRLEN bytes
// return 0 on success
// return -errno on failure to stat, open, or read
static int vdev_device_read_vdevd_instance( char const* mountpoint, char const* dev_fullpath, char* instance_str ) {
   
   int rc = 0;
   char const* devpath = dev_fullpath + strlen(mountpoint);
   
   char* instance_attr_relpath = vdev_fullpath( VDEV_METADATA_PREFIX, devpath, NULL );
   if( instance_attr_relpath == NULL ) {
      
      return -ENOMEM;
   }
   
   char* instance_attr_path = vdev_fullpath( mountpoint, instance_attr_relpath, NULL );
   
   free( instance_attr_relpath );
   instance_attr_relpath = NULL;
   
   if( instance_attr_path == NULL ) {
      
      return -ENOMEM;
   }
   
   char* instance_path = vdev_fullpath( instance_attr_path, VDEV_METADATA_PARAM_INSTANCE, NULL );
   
   free( instance_attr_path );
   instance_attr_path = NULL;
   
   if( instance_path == NULL ) {
      
      return -ENOMEM;
   }
   
   // read the instance string 
   rc = vdev_read_file( instance_path, instance_str, VDEV_CONFIG_INSTANCE_NONCE_STRLEN - 1 );
   if( rc < 0 ) {
      
      vdev_error("vdev_read_file('%s') rc = %d\n", instance_path, rc );
   }
   
   free( instance_path );
   instance_path = NULL;
   
   return rc;
}


// scan callback for a directory.
// queue directories, and unlink device files that are no longer plugged in.
// return 0 on success
// return -ENOMEM on OOM
// NOTE: mask unlink() failures, but log them.
static int vdev_remove_unplugged_device( char const* path, void* cls ) {
   
   int rc = 0;
   struct stat sb;
   char instance_str[ VDEV_CONFIG_INSTANCE_NONCE_STRLEN + 1 ];
   
   memset( instance_str, 0, VDEV_CONFIG_INSTANCE_NONCE_STRLEN + 1 );
   
   // extract cls 
   struct vdev_device_unplug_context* ctx = (struct vdev_device_unplug_context*)cls;
   
   struct sglib_cstr_vector* device_paths = ctx->device_paths;
   struct vdev_state* state = ctx->state;
   
   if( strlen(path) == 0 ) {
      
      // nothing to do 
      return 0;
   }
   
   // is this . or ..?
   char* basename = vdev_basename( path, NULL );
   
   // skip . and ..
   if( strcmp(basename, ".") == 0 || strcmp(basename, "..") == 0 ) {
      free( basename );
      return 0;
   }
   
   free( basename );
   
   // what is this?
   rc = lstat( path, &sb );
   if( rc != 0 ) {
      
      vdev_error("stat('%s') rc = %d\n", path, rc );
      
      // mask
      return 0;
   }
   
   // is this a directory?
   if( S_ISDIR( sb.st_mode ) ) {

      // search this later 
      char* path_dup = vdev_strdup_or_null( path );
      if( path_dup == NULL ) {
         
         // really can't continue 
         return -ENOMEM;
      }
      
      sglib_cstr_vector_push_back( device_paths, path_dup );
      
      return 0;
   }
   
   // is this a device file?
   if( S_ISBLK( sb.st_mode ) || S_ISCHR( sb.st_mode ) ) {
      
      // what's the instance value?
      rc = vdev_device_read_vdevd_instance( state->config->mountpoint, path, instance_str );
      if( rc != 0 ) {
         
         vdev_error("vdev_device_read_vdevd_instance('%s') rc = %d\n", path, rc );
         
         // mask 
         return 0;
      }
      
      // does it match ours?
      if( strcmp( state->config->instance_str, instance_str ) != 0 ) {

         struct vdev_device_request* to_delete = NULL;
         char const* device_path = NULL;
         
         vdev_debug("Remove unplugged device '%s'\n", path );
         
         device_path = path + strlen( state->mountpoint );
         
         to_delete = VDEV_CALLOC( struct vdev_device_request, 1 );
         if( to_delete == NULL ) {
            
            // OOM 
            return -ENOMEM;
         }
         
         rc = vdev_device_request_init( to_delete, state, VDEV_DEVICE_REMOVE, device_path );
         if( rc != 0 ) {
            
            // OOM 
            return rc;
         }
         
         // populate 
         vdev_device_request_set_dev( to_delete, sb.st_rdev );
         vdev_device_request_set_mode( to_delete, S_ISBLK( sb.st_mode ) ? S_IFBLK : S_IFCHR );
         
         // remove it 
         rc = vdev_device_remove( to_delete );
         if( rc != 0 ) {
            
            vdev_warn("vdev_device_remove('%s') rc = %d\n", device_path, rc );
            rc = 0;
         }
      }
   }
   
   return 0;
}


// remove all devices that no longer exist--that is, the contents of the /dev/vdev/$DEVICE_PATH/dev_instance file 
// does not match this vdev's instance nonce.
// this is used when running with --once.
int vdev_remove_unplugged_devices( struct vdev_state* state ) {
   
   int rc = 0;
   struct sglib_cstr_vector device_paths;
   char* devroot = vdev_strdup_or_null( state->config->mountpoint );
   char* next_dir = NULL;
   size_t next_dir_index = 0;
   struct stat sb;
   
   struct vdev_device_unplug_context unplug_ctx;
   
   unplug_ctx.state = state;
   unplug_ctx.device_paths = &device_paths;
   
   if( devroot == NULL ) {
      return -ENOMEM;
   }
   
   sglib_cstr_vector_init( &device_paths );
   
   // walk /dev breadth-first
   rc = sglib_cstr_vector_push_back( &device_paths, devroot );
   if( rc != 0 ) {
      
      sglib_cstr_vector_free( &device_paths );
      free( devroot );
      
      return rc;
   }
   
   while( next_dir_index < sglib_cstr_vector_size( &device_paths ) ) {
      
      // next path 
      next_dir = sglib_cstr_vector_at( &device_paths, next_dir_index );
      sglib_cstr_vector_set( &device_paths, NULL, next_dir_index );
      
      next_dir_index++;
      
      // scan this directory, and remove unplugged device files and remember the directories to search
      rc = vdev_load_all( next_dir, vdev_remove_unplugged_device, &unplug_ctx );
      if( rc != 0 ) {
         
         vdev_error("vdev_load_all('%s') rc = %d\n", next_dir, rc );
         
         free( next_dir );
         break;
      }
      
      free( next_dir );
   }
   
   // free any unused vector space
   while( next_dir_index < sglib_cstr_vector_size( &device_paths ) ) {
      
      next_dir = sglib_cstr_vector_at( &device_paths, next_dir_index );
      sglib_cstr_vector_set( &device_paths, NULL, next_dir_index );
      
      next_dir_index++;
      
      free( next_dir );
   }
   
   sglib_cstr_vector_free( &device_paths );
   
   return rc;
}


// start up the back-end
// return 0 on success 
// return -ENOMEM on OOM 
// return negative if the OS-specific back-end fails to initialize
int vdev_start( struct vdev_state* vdev ) {
   
   int rc = 0;
   
   // otherwise, it's already given
   vdev->running = true;
   
   // initialize OS-specific state, and start feeding requests
   vdev->os = VDEV_CALLOC( struct vdev_os_context, 1 );
   
   if( vdev->os == NULL ) {
      
      return -ENOMEM;
   }
   
   // start processing requests 
   rc = vdev_wq_start( &vdev->device_wq );
   if( rc != 0 ) {
      
      vdev_error("vdev_wq_start rc = %d\n", rc );
      
      free( vdev->os );
      vdev->os = NULL;
      return rc;
   }
   
   
   rc = vdev_os_context_init( vdev->os, vdev );
   
   if( rc != 0 ) {
   
      vdev_error("vdev_os_context_init rc = %d\n", rc );
      
      int wqrc = vdev_wq_stop( &vdev->device_wq, false );
      if( wqrc != 0 ) {
         
         vdev_error("vdev_wq_stop rc = %d\n", wqrc);
      }
      
      free( vdev->os );
      vdev->os = NULL;
      return rc;
   }
   
   return 0;
}


// run the pre-seed command, if given 
// return 0 on success
// return -ENOMEM on OOM 
// return non-zero on non-zero exit status
int vdev_preseed_run( struct vdev_state* vdev ) {
   
   int rc = 0;
   int exit_status = 0;
   char* command = NULL;
   
   if( vdev->config->preseed_path == NULL ) {
      // nothing to do 
      return 0;
   }
   
   command = VDEV_CALLOC( char, strlen( vdev->config->preseed_path ) + 2 + strlen( vdev->config->mountpoint ) + 1 );
   if( command == NULL ) {
      
      // OOM
      return -ENOMEM;
   }
   
   sprintf(command, "%s %s", vdev->config->preseed_path, vdev->config->mountpoint );
   
   rc = vdev_subprocess( command, NULL, NULL, 0, &exit_status );
   if( rc != 0 ) {
      
      vdev_error("vdev_subprocess('%s') rc = %d\n", command, rc );
   }
   else if( exit_status != 0 ) {
      
      vdev_error("vdev_subprocess('%s') exit status %d\n", command, exit_status );
      rc = exit_status;
   }
   
   free( command );
   command = NULL;
   
   return rc;
}


// global vdev initialization 
int vdev_init( struct vdev_state* vdev, int argc, char** argv ) {
   
   // global setup 
   vdev_setup_global();
   
   int rc = 0;
   
   int fuse_argc = 0;
   char** fuse_argv = VDEV_CALLOC( char*, argc + 1 );
   
   if( fuse_argv == NULL ) {
      
      return -ENOMEM;
   }
   
   // config...
   vdev->config = VDEV_CALLOC( struct vdev_config, 1 );
   if( vdev->config == NULL ) {
      
      free( fuse_argv );
      return -ENOMEM;
   }
   
   // config init
   rc = vdev_config_init( vdev->config );
   if( rc != 0 ) {
      
      vdev_error("vdev_config_init rc = %d\n", rc );
      return rc;
   }
   
   // parse config options from command-line 
   rc = vdev_config_load_from_args( vdev->config, argc, argv, &fuse_argc, fuse_argv );
   
   // not needed for vdevd
   free( fuse_argv );
   
   if( rc != 0 ) {
      
      vdev_error("vdev_config_load_from_argv rc = %d\n", rc );
      
      vdev_config_usage( argv[0] );
      
      return rc;
   }
   
   // if we didn't get a config file, use the default one
   if( vdev->config->config_path == NULL ) {
      
      vdev->config->config_path = vdev_strdup_or_null( VDEV_CONFIG_FILE );
      if( vdev->config->config_path == NULL ) {

         // OOM 
         return -ENOMEM;
      }
   }
   
   vdev_set_debug_level( vdev->config->debug_level );
   vdev_set_error_level( vdev->config->error_level );
   
   vdev_info("Config file:      '%s'\n", vdev->config->config_path );
   vdev_info("Log debug level:  '%s'\n", (vdev->config->debug_level == VDEV_LOGLEVEL_DEBUG ? "debug" : (vdev->config->debug_level == VDEV_LOGLEVEL_INFO ? "info" : "none")) );
   vdev_info("Log error level:  '%s'\n", (vdev->config->error_level == VDEV_LOGLEVEL_WARN ? "warning" : (vdev->config->error_level == VDEV_LOGLEVEL_ERROR ? "error" : "none")) );
   
   // load from file...
   rc = vdev_config_load( vdev->config->config_path, vdev->config );
   if( rc != 0 ) {
      
      vdev_error("vdev_config_load('%s') rc = %d\n", vdev->config->config_path, rc );
      
      return rc;
   }
   
   // if no command-line loglevel is given, then take it from the config file (if given)
   if( vdev->config->debug_level != VDEV_LOGLEVEL_NONE ) {
      
      vdev_set_debug_level( vdev->config->debug_level );
   }
   
   if( vdev->config->error_level != VDEV_LOGLEVEL_NONE ) {
      
      vdev_set_error_level( vdev->config->error_level );
   }
   
   // convert to absolute paths 
   rc = vdev_config_fullpaths( vdev->config );
   if( rc != 0 ) {
      
      vdev_error("vdev_config_fullpaths rc = %d\n", rc );
      
      return rc;
   }
   
   vdev_info("vdev actions dir: '%s'\n", vdev->config->acts_dir );
   vdev_info("firmware dir:     '%s'\n", vdev->config->firmware_dir );
   vdev_info("helpers dir:      '%s'\n", vdev->config->helpers_dir );
   vdev_info("logfile path:     '%s'\n", vdev->config->logfile_path );
   vdev_info("pidfile path:     '%s'\n", vdev->config->pidfile_path );
   vdev_info("default mode:      0%o\n", vdev->config->default_mode );
   vdev_info("ifnames file:     '%s'\n", vdev->config->ifnames_path );
   vdev_info("preseed script:   '%s'\n", vdev->config->preseed_path );
   
   vdev->mountpoint = vdev_strdup_or_null( vdev->config->mountpoint );
   vdev->once = vdev->config->once;
   
   if( vdev->mountpoint == NULL ) {
      
      vdev_error("Failed to set mountpoint, config->mountpount = '%s'\n", vdev->config->mountpoint );
      
      return -EINVAL;
   }
   else {
      
      vdev_info("mountpoint:       '%s'\n", vdev->mountpoint );
   }
   
   vdev->argc = argc;
   vdev->argv = argv;
   
   // load actions 
   rc = vdev_action_load_all( vdev->config->acts_dir, &vdev->acts, &vdev->num_acts );
   if( rc != 0) {
      
      vdev_error("vdev_action_load_all('%s') rc = %d\n", vdev->config->acts_dir, rc );
      
      return rc;
   }
   
   // initialize request work queue 
   rc = vdev_wq_init( &vdev->device_wq, vdev );
   if( rc != 0 ) {
      
      vdev_error("vdev_wq_init rc = %d\n", rc );
      
      return rc;
   }
   
   return 0;
}


// main loop for the back-end 
// return 0 on success
// return -errno on failure to daemonize, or abnormal OS-specific back-end failure
int vdev_main( struct vdev_state* vdev, int flush_fd ) {
   
   int rc = 0;
   
   char* metadata_dir = vdev_device_metadata_fullpath( vdev->mountpoint, "" );
   if( metadata_dir == NULL ) {
      
      return -ENOMEM;
   }
   
   // create metadata directory 
   rc = vdev_mkdirs( metadata_dir, 0, 0755 );
   
   if( rc != 0 ) {
      
      vdev_error("vdev_mkdirs('%s') rc = %d\n", metadata_dir, rc );
      
      free( metadata_dir );
      return rc;
   }
   
   free( metadata_dir );
   
   vdev->flush_fd = flush_fd;
   
   rc = vdev_os_main( vdev->os );
   
   return rc;
}


// signal that the device work queue has flushed all initial devices 
// always succeeds 
int vdev_signal_wq_flushed( struct vdev_state* state ) {
   
   if( state->flush_fd >= 0) {
      
      // wake up anyone waiting for the workqueue to be drained
      int rc = 0;
      write( state->flush_fd, &rc, sizeof(rc) );
      
      close( state->flush_fd );
      state->flush_fd = -1;
   }
   
   return 0;
}


// stop vdev 
// NOTE: if this fails, there's not really a way to recover
int vdev_stop( struct vdev_state* vdev ) {
   
   int rc = 0;
   bool wait_for_empty = false;
   
   if( !vdev->running ) {
      return -EINVAL;
   }
   
   vdev->running = false;
   wait_for_empty = vdev->once;         // wait for the queue to drain if running once
   
   // stop processing requests 
   rc = vdev_wq_stop( &vdev->device_wq, wait_for_empty );
   if( rc != 0 ) {
      
      vdev_error("vdev_wq_stop rc = %d\n", rc );
      return rc;
   }
   
   return rc;
}

// free up vdev 
int vdev_shutdown( struct vdev_state* vdev, bool unlink_pidfile ) {
   
   if( vdev->running ) {
      return -EINVAL;
   }
   
   // remove the PID file, if we have one 
   if( vdev->config->pidfile_path != NULL && unlink_pidfile ) {
      unlink( vdev->config->pidfile_path );
   }
   
   vdev_action_free_all( vdev->acts, vdev->num_acts );
   
   vdev->acts = NULL;
   vdev->num_acts = 0;
   
   if( vdev->os != NULL ) {
      vdev_os_context_free( vdev->os );
      free( vdev->os );
      vdev->os = NULL;
   }
   
   if( vdev->config != NULL ) {
      vdev_config_free( vdev->config );
      free( vdev->config );
      vdev->config = NULL;
   }
   
   vdev_wq_free( &vdev->device_wq );
   
   if( vdev->mountpoint != NULL ) {
      free( vdev->mountpoint );
      vdev->mountpoint = NULL;
   }
   
   return 0;
}
