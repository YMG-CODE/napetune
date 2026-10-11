"""Read-only serial monitor. Set DTR/RTS OFF before opening the port."""
import argparse
import sys
import serial


def configured_port(factory, port, baud):
    connection = factory(port=None, baudrate=baud, timeout=0.2,
                         write_timeout=0, rtscts=False, dsrdtr=False)
    connection.dtr = False
    connection.rts = False
    connection.port = port
    return connection


def self_test():
    events = []

    class FakePort:
        def __init__(self, **kwargs):
            assert kwargs['port'] is None
            self.__dict__.update(kwargs)

        def __setattr__(self, name, value):
            events.append((name, value))
            self.__dict__[name] = value

        def open(self):
            assert self.dtr is False and self.rts is False
            events.append(('open', self.port))

    connection = configured_port(FakePort, 'TEST', 115200)
    connection.open()
    assert events == [('dtr', False), ('rts', False), ('port', 'TEST'), ('open', 'TEST')]
    print('PASS: DTR/RTS are OFF before open; no data writes or reset pulses.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM10')
    parser.add_argument('--baud', type=int, default=115200)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    connection = configured_port(serial.Serial, args.port, args.baud)
    try:
        connection.open()
        print(f'OPEN {args.port}: DTR=OFF RTS=OFF. Ctrl+C closes. No bytes sent.', flush=True)
        while True:
            data = connection.read(1024)
            if data:
                sys.stdout.buffer.write(data)
                sys.stdout.buffer.flush()
    except KeyboardInterrupt:
        return 0
    except serial.SerialException as error:
        print(f'Port error: {error}. Close other monitors first.', file=sys.stderr)
        return 1
    finally:
        if connection.is_open:
            # Signals are already OFF: do not pulse either line at shutdown.
            connection.close()
            print('\nCLOSED with no explicit reset pulse.', flush=True)


if __name__ == '__main__':
    raise SystemExit(main())
