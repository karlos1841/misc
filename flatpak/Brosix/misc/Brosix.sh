#!/bin/bash
if [ ! -e "/tmp/Brosix" ]
then
    ln -s $XDG_DATA_HOME /tmp/Brosix
fi
exec /app/bin/Brosix
