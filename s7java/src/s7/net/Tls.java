// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later

package s7.net;

import java.io.ByteArrayInputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.KeyFactory;
import java.security.KeyStore;
import java.security.MessageDigest;
import java.security.PrivateKey;
import java.security.cert.Certificate;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.security.spec.PKCS8EncodedKeySpec;
import java.util.ArrayList;
import java.util.Base64;
import java.util.Collection;
import java.util.HexFormat;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import javax.net.ssl.KeyManagerFactory;
import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLSession;
import javax.net.ssl.TrustManagerFactory;

/** mTLS from PEM files, and a node's identity: the spiffe://s7/node/<uuid> SAN URI of its certificate. */
public final class Tls {
    private static final String NODE_URI_PREFIX = "spiffe://s7/node/";

    private Tls() {
    }

    private static byte[] pem(String text, String label) {
        Matcher m = Pattern.compile("-----BEGIN " + label + "-----(.*?)-----END " + label + "-----", Pattern.DOTALL).matcher(text);
        if (!m.find()) {
            throw new IllegalArgumentException("no " + label + " in PEM");
        }
        return Base64.getMimeDecoder().decode(m.group(1));
    }

    static List<X509Certificate> certificates(Path file) throws Exception {
        CertificateFactory factory = CertificateFactory.getInstance("X.509");
        List<X509Certificate> out = new ArrayList<>();
        for (Certificate c : factory.generateCertificates(new ByteArrayInputStream(Files.readAllBytes(file)))) {
            out.add((X509Certificate) c);
        }
        return out;
    }

    static PrivateKey privateKey(Path file) throws Exception {
        PKCS8EncodedKeySpec spec = new PKCS8EncodedKeySpec(pem(Files.readString(file), "PRIVATE KEY"));
        for (String algorithm : new String[]{"RSA", "EC", "Ed25519"}) {
            try {
                return KeyFactory.getInstance(algorithm).generatePrivate(spec);
            } catch (java.security.spec.InvalidKeySpecException ignored) {
                // another key type
            }
        }
        throw new IllegalArgumentException("unsupported private key in " + file);
    }

    /** A TLS context with this node's certificate and key, trusting only the given CA. */
    public static SSLContext context(Path cert, Path key, Path ca) throws Exception {
        char[] password = new char[0];
        KeyStore identity = KeyStore.getInstance("PKCS12");
        identity.load(null, null);
        identity.setKeyEntry("node", privateKey(key), password, certificates(cert).toArray(new Certificate[0]));
        KeyManagerFactory keys = KeyManagerFactory.getInstance(KeyManagerFactory.getDefaultAlgorithm());
        keys.init(identity, password);
        KeyStore trust = KeyStore.getInstance("PKCS12");
        trust.load(null, null);
        int index = 0;
        for (X509Certificate c : certificates(ca)) {
            trust.setCertificateEntry("ca" + index++, c);
        }
        TrustManagerFactory trusted = TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm());
        trusted.init(trust);
        SSLContext context = SSLContext.getInstance("TLS");
        context.init(keys.getKeyManagers(), trusted.getTrustManagers(), null);
        return context;
    }

    /**
     * The node UUID of the certificate's only SAN URI that starts with spiffe://s7/node/, as the
     * C++ node's {@code node_uuid_of}: the rest of the URI, compared exactly; "" (an unidentified
     * peer) when there is no such URI or more than one, even if they are equal.
     */
    public static String nodeUuid(X509Certificate certificate) {
        try {
            return nodeUuidOf(certificate.getSubjectAlternativeNames());
        } catch (java.security.cert.CertificateParsingException ex) {
            return "";
        }
    }

    /** {@link #nodeUuid} over a certificate's subject alternative names ({type, value} pairs; null: none). */
    public static String nodeUuidOf(Collection<List<?>> names) {
        String found = null;
        for (List<?> name : names != null ? names : List.<List<?>>of()) {
            if ((Integer) name.get(0) == 6 && ((String) name.get(1)).startsWith(NODE_URI_PREFIX)) { // uniformResourceIdentifier
                if (found != null) {
                    return ""; // more than one node identity
                }
                found = ((String) name.get(1)).substring(NODE_URI_PREFIX.length());
            }
        }
        return found != null ? found : "";
    }

    public static String fingerprint(X509Certificate certificate) {
        try {
            return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(certificate.getEncoded()));
        } catch (Exception ex) {
            throw new IllegalStateException(ex);
        }
    }

    /** The verified peer certificate of a TLS session, or null. */
    public static X509Certificate peer(SSLSession session) {
        try {
            Certificate[] chain = session.getPeerCertificates();
            return chain.length > 0 ? (X509Certificate) chain[0] : null;
        } catch (javax.net.ssl.SSLPeerUnverifiedException ex) {
            return null;
        }
    }

    public static String nodeUuidOfFile(Path cert) throws Exception {
        return nodeUuid(certificates(cert).get(0));
    }
}
