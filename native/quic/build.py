#!/usr/bin/env python3
"""Build the pinned general QUIC provider. Windows uses Build-Quic.ps1."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
ROOT = Path(__file__).resolve().parents[2]
def verify():
    manifest=json.loads((ROOT/'vendor/DLN_QUIC_SOURCES.json').read_text())
    for key,directory in [('ngtcp2','ngtcp2'),('gnutls_headers','gnutls-headers')]:
        for name,digest in manifest[key]['files'].items():
            path=ROOT/'vendor'/directory/name
            if hashlib.sha256(path.read_bytes()).hexdigest()!=digest:raise RuntimeError('Modified QUIC dependency: '+str(path))
def build(output,extra=()):
    verify();v=ROOT/'vendor/ngtcp2'
    source=sorted((v/'lib').glob('*.c'))+[v/'crypto/shared.c',v/'crypto/gnutls/gnutls.c',Path(__file__).with_name('provider.c')]
    command=[os.environ.get('CC','cc'),'-std=gnu11','-O1','-g','-fPIC','-fvisibility=hidden','-DNGTCP2_STATICLIB','-DHAVE_ARPA_INET_H','-DHAVE_NETINET_IN_H']
    for include in [v/'lib/includes',v/'lib',v/'crypto/includes',v/'crypto',ROOT/'vendor/gnutls-headers']:command+=['-I',str(include)]
    command+=list(extra)+[str(p) for p in source]+['-shared','-Wl,-z,defs','-lgnutls','-o',str(output)]
    subprocess.run(command,check=True);return output
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('output',type=Path);parser.add_argument('--sanitize',action='store_true');args=parser.parse_args()
    build(args.output,('-fsanitize=address,undefined','-fno-omit-frame-pointer') if args.sanitize else ())
