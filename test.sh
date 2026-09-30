# printf '                            METHOD!        /   HTTP/1.1\r\nHost: xetera.dev\r\nAccept: */*\r\n\r\n' | nc localhost 80
# printf 'POST / HTTP/1.1\r\nHost: xetera.dev\r\nAccept: */*\r\nContent-Length: 9\r\n\r\n123456789' | nc localhost 80
# printf 'GET /a HTTP/1.1\r\nHost: x\r\n\r\nGET /b HTTP/1.1\r\nHost: x\r\n\r\n' | nc localhost 80
printf 'GET / HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n' | nc localhost 80 | od -c
