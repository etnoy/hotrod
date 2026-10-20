#!/usr/bin/env python3
"""Local HTTP client for the replacement bridge (not the stock APK/API).

IDVN_URL=http://device-address IDVN_API_TOKEN=... python3 tools/idvn.py status
"""
import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import sys
import time
from urllib.error import HTTPError, URLError
from urllib.parse import urlparse
from urllib.request import Request, urlopen


def request(base,token,path,body=None):
    headers={'Authorization':'Bearer '+token,'Accept':'application/json'}
    data=None
    if body is not None:
        headers['Content-Type']='application/json'
        data=json.dumps(body).encode()
    req=Request(base+path,data=data,headers=headers,method='POST' if body is not None else 'GET')
    try:
        with urlopen(req,timeout=3) as response:
            return json.load(response)
    except HTTPError as error:
        detail=error.read(2048).decode(errors='replace')
        raise RuntimeError(f'HTTP {error.code}: {detail}') from error


def wait_operation(base,token,identifier):
    deadline=time.monotonic()+5
    while time.monotonic()<deadline:
        result=request(base,token,f'/v1/operation?id={identifier}')
        operation=result['operation']
        if operation['status']=='applied':
            return result
        if operation['status'] not in ['queued','sent']:
            raise RuntimeError('Operation failed or uncertain: '+json.dumps(operation))
        time.sleep(0.15)
    raise RuntimeError('Operation result timed out; settings may have changed. Read data before retrying.')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest='command',required=True)
    for command in ['status','data','identify','sync-time']:
        sub.add_parser(command)
    modes={'departure':0,'reduction':1,'timer':2}
    mode=sub.add_parser('mode')
    mode.add_argument('--left',choices=modes,required=True)
    mode.add_argument('--right',choices=modes,required=True)
    mode.add_argument('--timers',type=int,choices=[1,2,3],required=True)
    mode.add_argument('--period',type=int,choices=[60,30,15],required=True)
    for command in ['working','departure','reduction']:
        sub.add_parser(command).add_argument('json_file',type=Path)
    args=parser.parse_args()
    base=os.environ.get('IDVN_URL','').rstrip('/')
    token=os.environ.get('IDVN_API_TOKEN','')
    parsed=urlparse(base)
    if parsed.scheme not in ['http','https'] or not parsed.netloc or parsed.query or parsed.fragment or parsed.username:
        parser.error('Set IDVN_URL to the bridge URL, for example http://192.168.1.80')
    if not token:
        parser.error('Set IDVN_API_TOKEN to the configured local API token')
    body=None; endpoint=args.command
    if args.command=='mode':
        body={'left':modes[args.left],'right':modes[args.right],'timers':args.timers,
              'period':{60:0,30:1,15:2}[args.period]}
    elif args.command=='identify':
        body={}
    elif args.command=='sync-time':
        now=datetime.now().astimezone(); endpoint='time'
        body={'year':now.year%100,'month':now.month,'day':now.day,'weekDay':now.isoweekday(),
              'hours':now.hour,'minutes':now.minute,'seconds':now.second}
    elif args.command in ['working','departure','reduction']:
        body=json.loads(args.json_file.read_text())
    result=request(base,token,'/v1/'+endpoint,body)
    if body is not None:
        result=wait_operation(base,token,result['operation']['id'])
    print(json.dumps(result,indent=2))


if __name__=='__main__':
    try:
        main()
    except (RuntimeError,URLError,OSError,ValueError) as error:
        print(str(error),file=sys.stderr)
        sys.exit(1)
