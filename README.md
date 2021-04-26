# AzulNX2Ethernet
QLogic NetXtreme II driver for macOS

This driver is a work-in-progress that aims to support the various 1GB QLogic (formerlly Broadcom) NetXtreme II server-grade network cards. This driver is based primarily on the FreeBSD `bce` driver, with the Linux `bnx2` driver and the Broadcom programmers guide for the NetXtreme II used as additional references.

## Supported cards
| Card     | PCI Device ID |
|----------|-----------|
| BCM5706  | 14E4:164A |
| BCM5706S | 14E4:16AA |
| BCM5708  | 14E4:164C |
| BCM5708S | 14E4:16AC |
| BCM5709  | 14E4:1639 |
| BCM5709S | 14E4:163A |
| BCM5716  | 14E4:163B |

Various HP-branded cards (subsystem vendor ID of `0x103C`) of the above are also supported.
