"""Check a device's OTA endpoint, or explicitly upload a project application image."""
import argparse
import http.client
import json
from pathlib import Path
import socket
from urllib.parse import urlsplit


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', default='http://192.168.4.1')
    parser.add_argument('--mode', choices=['check', 'negative', 'upload'], default='check')
    parser.add_argument('--file', type=Path)
    args = parser.parse_args()
    url = urlsplit(args.url)
    if url.scheme != 'http' or not url.hostname:
        raise ValueError('Use the local device HTTP address')

    def connect():
        return http.client.HTTPConnection(url.hostname, url.port or 80, timeout=90)

    def request(method, path, data=None, marker=True):
        conn = connect()
        headers = {'Content-Type': 'application/octet-stream'}
        if marker:
            headers['X-ComputerScreen-Setup'] = '1'
            headers['X-Case-Setup'] = '1'
        conn.request(method, path, data, headers)
        response = conn.getresponse()
        result = response.status, response.read().decode('utf-8', 'replace')
        conn.close()
        return result

    status, body = request('GET', '/api/firmware')
    assert status == 200, (status, body)
    initial = json.loads(body)
    print('Firmware:', json.dumps(initial, ensure_ascii=False), flush=True)
    if args.mode == 'check':
        return
    if not args.file:
        parser.error('--file is required for negative/upload tests')
    data = args.file.read_bytes()
    if args.mode == 'upload':
        status, message = request('POST', '/api/update', data)
        print('Upload:', status, message, flush=True)
        assert status == 200, (status, message)
        return

    def rejected(label, payload, marker=True):
        status, message = request('POST', '/api/update', payload, marker)
        print(label, status, message, flush=True)
        assert status == 400, (status, message)

    rejected('Missing same-origin header:', b'x' * 512, False)
    rejected('Not an ESP image:', b'x' * 512)
    wrong_chip = bytearray(data[:512])
    wrong_chip[12] = 2
    rejected('Wrong chip:', wrong_chip)
    old_version = bytearray(data[:512])
    old_version[48:80] = b'old-release' + b'\0' * 21
    rejected('Pre-OTA release:', old_version)

    conn = connect()
    conn.putrequest('POST', '/api/update')
    conn.putheader('Content-Type', 'application/octet-stream')
    conn.putheader('X-ComputerScreen-Setup', '1')
    conn.putheader('X-Case-Setup', '1')
    conn.putheader('Content-Length', str(len(data)))
    conn.endheaders()
    conn.send(data[:512])
    conn.sock.shutdown(socket.SHUT_WR)
    response = conn.getresponse()
    message = response.read().decode('utf-8', 'replace')
    print('Interrupted upload:', response.status, message, flush=True)
    assert response.status == 400, (response.status, message)
    conn.close()

    damaged = bytearray(data)
    damaged[-1] ^= 1
    rejected('Corrupted image:', damaged)
    status, body = request('GET', '/api/firmware')
    final = json.loads(body)
    assert status == 200 and final['partition'] == initial['partition'] and final['version'] == initial['version']
    print('Negative tests passed; original firmware is still running.', flush=True)


if __name__ == '__main__':
    main()
