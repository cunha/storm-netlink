# storm-netlink

This is a simple C program to create `rtnetlink` storms (it just
creates routes and removes the same set of routes over and over
again).  This was used to troubleshoot performance problems in the
[PEERING testbed][1]; it is not the fastest (but fast enough for our
purposes) and not general at all.

 [1]: http://peering.usc.edu


