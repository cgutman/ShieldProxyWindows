Nvidia Shield Streaming Proxy for Windows
=========================================

Running the proxy:
1) Ensure that WinPcap is installed on your machine. It can be downloaded from http://www.winpcap.org/install/default.htm
2) Add the appropriate NAT forwarding rules on your router for the MDNS relay port (UDP 5354) and other Shield streaming ports (listed below)
3) Run the ShieldProxy.exe executable on the streaming PC
4) Run the Shield Proxy Android app on the Shield and specify the address of the streaming PC (accessible over the Internet which may be your router address)

Known Shield streaming ports:
TCP 35043
TCP 47989
TCP 47991
TCP 47995
TCP 47996

UDP 47998
UDP 47999
UDP 48000


Getting the code:
- The Shield Streaming Proxy for Windows code is available at https://github.com/cgutman/ShieldProxyWindows
- The Shield Streaming Proxy for Android code is available at https://github.com/cgutman/ShieldProxyAndroid

Building the proxy:
1) Install Visual Studio 2013 (previous versions may work)
2) Ensure the Lib and Include folders from the WinPcap developer pack are in a directory called WinPcap in the root of the repo
   The developer pack can be downloaded from http://www.winpcap.org/devel.htm
3) Open and build the solution in Visual Studio
