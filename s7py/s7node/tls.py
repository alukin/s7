# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""TLS server context and certificate identity (mTLS, S7_Control_Protocol_MVP.md section 2).

Certificate fields the standard library does not expose (Extended Key Usage)
are read with the `cryptography` package, which TLS mode requires.
"""
import hashlib
import re
import ssl
from dataclasses import dataclass, field

NODE_URI = re.compile(r"^spiffe://s7/node/([0-9a-fA-F-]{36})$")


@dataclass
class PeerCertificate:
    fingerprint_sha256: str
    san_uris: list[str] = field(default_factory=list)
    key_purpose_oids: list[str] = field(default_factory=list)
    common_name: str | None = None


@dataclass
class ControlPeer:
    """The authenticated peer of a control connection: what authorization may use."""

    tls: bool = False
    certificate: PeerCertificate | None = None


def _x509():
    try:
        from cryptography import x509
    except ImportError as error:
        raise RuntimeError("TLS needs the 'cryptography' package (pip install cryptography)") from error
    return x509


def describe_certificate(der: bytes) -> PeerCertificate:
    x509 = _x509()
    certificate = x509.load_der_x509_certificate(der)
    info = PeerCertificate(fingerprint_sha256=hashlib.sha256(der).hexdigest())
    try:
        san = certificate.extensions.get_extension_for_class(x509.SubjectAlternativeName).value
        info.san_uris = list(san.get_values_for_type(x509.UniformResourceIdentifier))
    except x509.ExtensionNotFound:
        pass
    try:
        eku = certificate.extensions.get_extension_for_class(x509.ExtendedKeyUsage).value
        info.key_purpose_oids = [oid.dotted_string for oid in eku]
    except x509.ExtensionNotFound:
        pass
    names = certificate.subject.get_attributes_for_oid(x509.NameOID.COMMON_NAME)
    info.common_name = str(names[0].value) if names else None
    return info


def load_certificate_file(path: str) -> PeerCertificate:
    with open(path, "rb") as file:
        der = ssl.PEM_cert_to_DER_cert(file.read().decode("ascii"))
    return describe_certificate(der)


def certificate_node_uuid(certificate: PeerCertificate) -> str:
    """The node UUID of the single SAN URI spiffe://s7/node/<node_uuid>."""
    uuids = {m.group(1).lower() for uri in certificate.san_uris if (m := NODE_URI.match(uri))}
    if len(uuids) != 1:
        raise ValueError("the certificate must carry exactly one SAN URI spiffe://s7/node/<node_uuid>")
    return uuids.pop()


def server_context(cert_file: str, key_file: str, ca_file: str) -> ssl.SSLContext:
    """Client certificates are optional (read-only requests need none) but verified when sent."""
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(cert_file, key_file)
    context.load_verify_locations(ca_file)
    context.verify_mode = ssl.CERT_OPTIONAL
    return context


def client_context(ca_file: str, cert_file: str | None = None, key_file: str | None = None) -> ssl.SSLContext:
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.load_verify_locations(ca_file)
    context.check_hostname = False  # nodes are identified by their SAN URI, not a host name
    if cert_file:
        context.load_cert_chain(cert_file, key_file)
    return context


def control_peer(ssl_object) -> ControlPeer:
    if ssl_object is None:
        return ControlPeer()
    der = ssl_object.getpeercert(binary_form=True)  # only returned after chain verification
    return ControlPeer(tls=True, certificate=describe_certificate(der) if der else None)
