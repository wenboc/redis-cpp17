xredis is s c++11   compatible with redis or hiredis_client multithreading  , in-memory data structure store, used as a database, cache  It supports data structures such as set get hset hget hkeys keys  hgetall dbsize flushdb sync slaveof save quit info auth config  zadd zrange sadd scard zrevragne publish unsubscribe subscribe  detailed test method reference redis benchmark https://redis.io/topics/benchmarks  https://stackoverflow.com/questions/2873249/is-memcached-a-dinosaur-in-comparison-to-redis

make  ./redis-server 127.0.0.1 6379 0
