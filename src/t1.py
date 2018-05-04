from cocaine.services import Service

from tornado import gen
from tornado.ioloop import IOLoop

import msgpack


DEFAULT_SERVICE = 'echo.orig1'
DEFAULT_MESSAGE = 'msg'

DEFAULT_SLEEP = 0.2


@gen.coroutine
def ping_loop():
    echo = Service(DEFAULT_SERVICE)

    ch = yield echo.enqueue('version')
    version = yield ch.rx.get(timeout=5)

    print 'version: {}'.format(version)

    cnt = 1
    while True:
        msg = '{}_{}'.format(DEFAULT_MESSAGE, cnt)
        cnt += 1

        # msg = msgpack.packb(msg)

        print 'sending ping message {}'.format(msg)

        ch = yield echo.enqueue('ping')

        _ = yield ch.tx.write(msg)
        answer = yield ch.rx.get(timeout=10)

        print 'ans {}'.format(answer)

        yield gen.sleep(DEFAULT_SLEEP)


print 'Running client...'
IOLoop.current().run_sync(ping_loop)
