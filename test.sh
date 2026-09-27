# printf '                            METHOD!        /   HTTP/1.1\r\nHost: xetera.dev\r\nAccept: */*\r\n\r\n' | nc localhost 80
printf 'POST / HTTP/1.1\r\nHost: xetera.dev\r\nAccept: */*\r\nContent-Length: 9\r\n\r\n123456789' | nc localhost 80
