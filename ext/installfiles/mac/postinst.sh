#!/bin/bash

export PATH=/bin:/usr/bin:/sbin:/usr/sbin

launchctl unload /Library/LaunchDaemons/com.zerotier.one.plist >>/dev/null 2>&1
sleep 1
killall zerotier-one
sleep 1
killall -9 zerotier-one

cd "/Library/Application Support/ZeroTier/One"
rm -rf node.log node.log.old root-topology shutdownIfUnreadable autoupdate.log updates.d
if [ ! -f authtoken.secret ]; then
	head -c 1024 /dev/urandom | md5 | head -c 24 >authtoken.secret
	chown root authtoken.secret
	chgrp wheel authtoken.secret
	chmod 0600 authtoken.secret
fi

launchctl load /Library/LaunchDaemons/com.zerotier.one.plist >>/dev/null 2>&1

exit 0
