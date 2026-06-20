# Test Cases — v0.9.1 (Networking Stack)

### Network Stack — test_net.cpp
- UDP socket lifecycle — Create, bind, send, and receive UDP datagrams; socket close releases resources
- ARP request/reply for gateway IP — ARP request sent to gateway; reply received and cache entry populated
- ARP cache expiration — Cached ARP entry expires after timeout; subsequent lookup triggers new ARP request
