
stunnel4 redis-client.conf 
stunnel redis-server.conf 

redis-server --port 16379 --requirepass boobared

sudo redis-cli -h 10.0.0.112 -p 16379
