# freeswitch-mod-cpg

*** ALERT *** 

this code does not work or does not even compile.

It has been published here only for reference, 
so please do not ask how to build it or how to update
to current corosync and/or freeswitch to make it work.

*** END OF ALERT *** 


freeswitch-mod-cpg is a module that provides coordination
between two freeswitch servers, in order to detect
a failure event and to take over resources,
by migrating ip address and sofia profiles on the failover node,
keeping calls alive, leveraging on sofia recover tools.
