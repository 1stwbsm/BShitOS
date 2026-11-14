#!/usr/bin/env python3
"""Convert a PEM certificate bundle into concatenated DER for /conf/roots.der.

usage: mkroots.py <output> <bundle.pem>...
With no bundle given, the host's system CA list is used.
"""
import base64
import os
import re
import sys

SYSTEM_BUNDLES = (
    "/etc/ssl/certs/ca-certificates.crt",
    "/etc/ssl/certs.pem",
    "/etc/ssl/cert.pem",
    "/etc/pki/tls/certs/ca-bundle.crt",
)
PEM = re.compile(rb"-----BEGIN CERTIFICATE-----(.+?)-----END CERTIFICATE-----", re.S)


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    output = sys.argv[1]
    bundles = sys.argv[2:] or [next((b for b in SYSTEM_BUNDLES if os.path.exists(b)), None)]
    if bundles[0] is None:
        raise SystemExit("mkroots: no system CA bundle found; pass one as an argument")

    seen = set()
    blob = bytearray()
    for bundle in bundles:
        with open(bundle, "rb") as f:
            for match in PEM.finditer(f.read()):
                der = base64.b64decode(b"".join(match.group(1).split()))
                if der not in seen:
                    seen.add(der)
                    blob += der
    if not seen:
        raise SystemExit("mkroots: no certificates found")

    with open(output, "wb") as f:
        f.write(blob)
    print(f"mkroots: {len(seen)} roots, {len(blob)} bytes")


if __name__ == "__main__":
    main()
