#!/bin/sh

# set up /dev/input/by-id and /dev/input/by-path

. $VDEV_HELPERS/subr.sh

# set up input device permissions...
# $1    input class
# return 0 on success
# return 1 on error
setup_input_permissions() {

   local MODE 
   
   # permissions...
   MODE=0660
   if [ "$INPUT_CLASS" = "js" ]; then 

      # joysticks are world-readable
      MODE=0664
   fi

   vdev_permissions root.input $MODE $VDEV_MOUNTPOINT/$VDEV_PATH
   return $?
}


# full sysfs path
SYSFS_PATH=$VDEV_OS_SYSFS_MOUNTPOINT/$VDEV_OS_DEVPATH

# if we don't know the device path, then there's not much we can do...
if [ "$VDEV_PATH" = "UNKNOWN" ]; then 
   exit 0
fi

# if removing, just blow away the symlinks
if [ "$VDEV_ACTION" = "remove" ]; then 
   
   vdev_rmlinks $VDEV_METADATA
   exit 0
fi


# stat the device!
eval $($VDEV_HELPERS/stat_input $VDEV_MOUNTPOINT/$VDEV_PATH)
STAT_RC=$?

# succeeded?
test 0 -ne $STAT_RC && vdev_fail 2 "stat_input $VDEV_PATH exit code $STAT_RC"

# get the persistent path for this device 
# should set VDEV_PERSISTENT_PATH
VDEV_PERSISTENT_PATH=
eval $($VDEV_HELPERS/stat_path $VDEV_MOUNTPOINT/$VDEV_PATH)

# input class?
INPUT_CLASS=$VDEV_INPUT_CLASS
CLASSLESS_EVDEV=0

if [ -z "$INPUT_CLASS" ]; then 
   
   # not a mouse, keyboard, or joystick.
   # pcspkr?
   INPUT_CLASS=$(test -n "$(vdev_drivers $SYSFS_PATH | /bin/grep "pcspkr")" && echo "spkr")
fi

if [ -z "$INPUT_CLASS" -a -f $SYSFS_PATH/name ]; then 
   
   # not a mouse, keyboard, joystick, or PC speaker 
   # ir device?  check the sysfs name
   INPUT_CLASS=$(/bin/cat $SYSFS_PATH/name | /bin/grep -i "dvb")
   if [ -n "$INPUT_CLASS" ]; then 
      
      INPUT_CLASS="ir"
   else 

      INPUT_CLASS=$(/bin/cat $SYSFS_PATH/name | /bin/grep " IR ")
      if [ -n "$INPUT_CLASS" ]; then 
      
         INPUT_CLASS="ir"
      fi
   fi
fi

# allow empty input class for event device nodes generated by platform and usb devices...
if [ -z "$INPUT_CLASS" -a -n "$(vdev_subsystems $SYSFS_PATH | /bin/egrep "usb|platform")" -a -n "$(echo $VDEV_OS_DEVNAME | /bin/egrep "event*")" ]; then 

   # evdev input device with an input class we cannot place
   CLASSLESS_EVDEV=1
fi

# no path?
if [ -z "$VDEV_PERSISTENT_PATH" ]; then 
   
   # just set up permissions 
   setup_input_permissions $INPUT_CLASS
   exit 0
fi


# /dev/input/by-path/ links
if [ -n "$INPUT_CLASS" ]; then

   # non-empty input class...
   # event?
   if [ -n "$(echo $VDEV_OS_DEVNAME | /bin/egrep "event*")" ]; then 

      vdev_symlink ../../$VDEV_PATH $VDEV_MOUNTPOINT/input/by-path/$VDEV_PERSISTENT_PATH-event-$INPUT_CLASS $VDEV_METADATA
   else 

      vdev_symlink ../../$VDEV_PATH $VDEV_MOUNTPOINT/input/by-path/$VDEV_PERSISTENT_PATH-$INPUT_CLASS $VDEV_METADATA
   fi

elif [ $CLASSLESS_EVDEV -ne 0 ]; then 
   
   # evdev input device with no input class
   vdev_symlink ../../$VDEV_PATH $VDEV_MOUNTPOINT/input/by-path/$VDEV_PERSISTENT_PATH-event $VDEV_METADATA
fi


# /dev/input/by-id links
BUS=
SERIAL=
IFNO=
BY_ID=

# so, is this a USB device?
if [ -n "$(echo $VDEV_OS_DEVPATH | /bin/grep 'usb')" ]; then 

   eval $($VDEV_HELPERS/stat_usb $VDEV_OS_SYSFS_MOUNTPOINT/$VDEV_OS_DEVPATH)

   if [ -n "$VDEV_USB_SERIAL" ]; then 

      SERIAL=$VDEV_USB_SERIAL
      IFNO=$VDEV_USB_INTERFACE_NUM
      BUS="usb"
   fi

fi

# TODO: are there more kinds of devices (non-usb) that have /dev/input/by-id links???

if [ -n "$BUS" -a -n "$SERIAL" ]; then 

   if [ -n "$INPUT_CLASS" ]; then 

      if [ -n "$(echo $VDEV_OS_DEVNAME | /bin/egrep "mouse|js")" ]; then 
         
         # mouse or joystick
         if [ -z "$IFNO" -o "$IFNO" = "00" ]; then 
               
            BY_ID="$BUS-$SERIAL-$INPUT_CLASS"
         else
            
            BY_ID="$BUS-$SERIAL-if${IFNO}-$INPUT_CLASS"
         fi

      elif [ -n "$(echo $VDEV_OS_DEVNAME | /bin/egrep "event")" ]; then 
         
         # evdev 
         if [ -z "$IFNO" -o "$IFNO" = "00" ]; then 

            BY_ID="$BUS-$SERIAL-event-$INPUT_CLASS"
         else
                     
            BY_ID="$BUS-$SERIAL-if${IFNO}-event-$INPUT_CLASS"
         fi
      fi
      
   elif [ $CLASSLESS_EVDEV -ne 0 -a -n "$IFNO" ]; then 
      
      # evdev input device with no class
      BY_ID="$BUS-$SERIAL-event-if${IFNO}"
   fi
fi

if [ -n "$BY_ID" ]; then 

   vdev_symlink ../../$VDEV_PATH $VDEV_MOUNTPOINT/input/by-id/$BY_ID $VDEV_METADATA 
fi


# permissions...
setup_input_permissions $INPUT_CLASS

exit 0
