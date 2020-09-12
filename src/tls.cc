/*
 * tls.c
 */

#include "extractor.h"
#include "json_object.h"
#include "tls.h"
#include "match.h"
#include "asn1/x509.h"

/* TLS Constants */

#define L_ContentType              1
#define L_ProtocolVersion          2
#define L_RecordLength             2
#define L_HandshakeType            1
#define L_HandshakeLength          3
#define L_ProtocolVersion          2
#define L_Random                  32
#define L_SessionIDLength          1
#define L_CipherSuiteVectorLength  2
#define L_CompressionMethodsLength 1
#define L_ExtensionsVectorLength   2
#define L_ExtensionType            2
#define L_ExtensionLength          2

#define L_NamedGroupListLen        2
#define L_ProtocolVersionListLen   1

/*
 * field lengths used in serverHello parsing
 */
#define L_CipherSuite              2
#define L_CompressionMethod        1
#define L_CertificateLength        3
#define L_CertificateListLength    3

/*
 * expanded set of static extensions
 */
#define num_static_extension_types 34

/*
 * extension types used in normalization
 */
#define type_sni                0x0000
#define type_supported_groups   0x000a
#define type_supported_versions 0x002b
#define type_session_ticket     0x0023

static uint16_t static_extension_types[num_static_extension_types] = {
        1,         /* max fragment length                    */
        5,         /* status_request                         */
        7,         /* client authz                           */
        8,         /* server authz                           */
        9,         /* cert type                              */
        10,        /* supported_groups                       */
        11,        /* ec_point_formats                       */
        13,        /* signature_algorithms                   */
        15,        /* heartbeat                              */
        16,        /* application_layer_protocol_negotiation */
        17,        /* status request v2                      */
        24,        /* token binding                          */
        27,        /* compressed certificate                 */
        28,        /* record size limit                      */
        43,        /* supported_versions                     */
        45,        /* psk_key_exchange_modes                 */
        50,        /* signature algorithms cert              */
        2570,      /* GREASE                                 */
        6682,      /* GREASE                                 */
        10794,     /* GREASE                                 */
        14906,     /* GREASE                                 */
        19018,     /* GREASE                                 */
        21760,     /* token binding (old)                    */
        23130,     /* GREASE                                 */
        27242,     /* GREASE                                 */
        31354,     /* GREASE                                 */
        35466,     /* GREASE                                 */
        39578,     /* GREASE                                 */
        43690,     /* GREASE                                 */
        47802,     /* GREASE                                 */
        51914,     /* GREASE                                 */
        56026,     /* GREASE                                 */
        60138,     /* GREASE                                 */
        64250      /* GREASE                                 */
    };

uint16_t degrease_uint16(uint16_t x);
void degrease_octet_string(void *data, ssize_t len);


void raw_as_hex_degrease(struct buffer_stream &buf, const void *data, size_t len) {
    if (len % 2) {
        return;  // error: len MUST be a multiple of two
    }
    uint16_t *x = (uint16_t *)data;
    uint16_t *x_end = x + (len/2);

    while (x < x_end) {
        uint16_t tmp = degrease_uint16(*x++);
        buf.raw_as_hex((const uint8_t *)&tmp, sizeof(tmp));
    }

}

void tls_security_assessment::print(struct json_object &o, const char *key) {
    struct json_array a{o, key};
    if (weak_version_offered) {
        a.print_string("weak_version_offered");
    }
    if (weak_ciphersuite_offered) {
        a.print_string("weak_ciphersuite_offered");
    }
}

struct tls_security_assessment tls_client_hello::security_assesment() {
    struct tls_security_assessment a;
    return a;
}

void tls_extensions::print(struct json_object &o, const char *key) const {

    struct datum ext_parser{this->data, this->data_end};

    struct json_array array{o, key};

    while (parser_get_data_length(&ext_parser) > 0) {
        size_t tmp_len = 0;
        size_t tmp_type;

        const uint8_t *data = ext_parser.data;
        if (parser_read_and_skip_uint(&ext_parser, L_ExtensionType, &tmp_type) == status_err) {
            break;
        }
        if (parser_read_and_skip_uint(&ext_parser, L_ExtensionLength, &tmp_len) == status_err) {
            break;
        }
        if (parser_skip(&ext_parser, tmp_len) == status_err) {
            break;
        }

        struct datum ext{data, ext_parser.data};
        array.print_hex(ext);

    }

    array.close();
}

void tls_extensions::print_server_name(struct json_object &o, const char *key) const {

    struct datum ext_parser{this->data, this->data_end};

    while (parser_get_data_length(&ext_parser) > 0) {
        size_t tmp_len = 0;
        size_t tmp_type;

        const uint8_t *data = ext_parser.data;
        if (parser_read_and_skip_uint(&ext_parser, L_ExtensionType, &tmp_type) == status_err) {
            break;
        }
        if (parser_read_and_skip_uint(&ext_parser, L_ExtensionLength, &tmp_len) == status_err) {
            break;
        }
        if (parser_skip(&ext_parser, tmp_len) == status_err) {
            break;
        }
        const uint8_t *data_end = ext_parser.data;

        if (tmp_type == type_sni) {
            struct datum ext{data, data_end};
            //            tls.print_key_json_string("server_name", pf.x.packet_data.value + SNI_HDR_LEN, pf.x.packet_data.length - SNI_HDR_LEN);
            // o.print_key_json_string(key, ext.data + SNI_HDR_LEN, ext.length() - SNI_HDR_LEN);
            ext.skip(SNI_HDR_LEN);
            o.print_key_json_string(key, ext);
        }
    }

}

void tls_extensions::set_server_name(struct datum &server_name) const {

    struct datum ext_parser{this->data, this->data_end};

    while (parser_get_data_length(&ext_parser) > 0) {
        size_t tmp_len = 0;
        size_t tmp_type;

        const uint8_t *data = ext_parser.data;
        if (parser_read_and_skip_uint(&ext_parser, L_ExtensionType, &tmp_type) == status_err) {
            break;
        }
        if (parser_read_and_skip_uint(&ext_parser, L_ExtensionLength, &tmp_len) == status_err) {
            break;
        }
        if (parser_skip(&ext_parser, tmp_len) == status_err) {
            break;
        }
        const uint8_t *data_end = ext_parser.data;

        if (tmp_type == type_sni) {
            struct datum ext{data, data_end};
            ext.skip(SNI_HDR_LEN);
            server_name = ext;
            return;
        }
    }

}

struct tls_extension {
    uint16_t type;
    uint16_t length;
    struct datum value;

    tls_extension(struct datum &p) : type{0}, length{0}, value{NULL, NULL} {

        if (p.read_uint16(&type) == false) { return; }
        if (p.read_uint16(&length) == false) { return; }
        if (length <= p.length()) {
            value.data = p.data;
            value.data_end = value.data + length;
            p.data += length;
        }
    }
};

void tls_extensions::fingerprint(struct buffer_stream &b) const {

    struct datum ext_parser{this->data, this->data_end};

    while (parser_get_data_length(&ext_parser) > 0) {

        const uint8_t *start_of_extension = ext_parser.data;
        tls_extension x{ext_parser};
        const uint8_t *end_of_extension = ext_parser.data;
        if (x.value.data == NULL) {
            break;
        }
        if (uint16_match(x.type, static_extension_types, num_static_extension_types) == true) {
            b.write_char('(');
            raw_as_hex_degrease(b, start_of_extension, L_ExtensionType);
            b.raw_as_hex(start_of_extension + L_ExtensionType, end_of_extension - start_of_extension - L_ExtensionType);
            b.write_char(')');
        } else {
            b.write_char('(');
            raw_as_hex_degrease(b, start_of_extension, L_ExtensionType);
            b.write_char(')');
        }

    }

}

void tls_extensions::print_session_ticket(struct json_object &o, const char *key) const {

    struct datum ext_parser{this->data, this->data_end};

    while (parser_get_data_length(&ext_parser) > 0) {
        size_t tmp_len = 0;
        size_t tmp_type;

        const uint8_t *data = ext_parser.data;
        if (parser_read_and_skip_uint(&ext_parser, L_ExtensionType, &tmp_type) == status_err) {
            break;
        }
        if (parser_read_and_skip_uint(&ext_parser, L_ExtensionLength, &tmp_len) == status_err) {
            break;
        }
        if (parser_skip(&ext_parser, tmp_len) == status_err) {
            break;
        }

        if (tmp_type == type_session_ticket) {

            // possible format, as per https://tools.ietf.org/html/rfc5077#section-4
            //
            // struct {
            //    opaque key_name[16];
            //    opaque iv[16];
            //    opaque encrypted_state<0..2^16-1>;
            //    opaque mac[32];
            // } ticket;

            struct datum ext{data + L_ExtensionType + L_ExtensionLength, ext_parser.data};
            o.print_key_hex(key, ext);
        }
    }

}

#define L_DTLSCookieLength             1

void tls_client_hello::parse(struct datum &p) {
    size_t tmp_len;

    extractor_debug("%s: processing packet\n", __func__);

    // parse clientHello.ProtocolVersion
    protocol_version.parse(p, L_ProtocolVersion);
    if (protocol_version.is_not_readable()) {
        return;
    }

    // determine if this is DTLS or plain old TLS
    if (protocol_version.data[0] == 0xfe) {
        dtls = true;
    }

    // parse clientHello.Random
    random.parse(p, L_Random);

    // parse SessionID
    if (parser_read_and_skip_uint(&p, L_SessionIDLength, &tmp_len) == status_err) {
        return;
    }
    session_id.parse(p, tmp_len);

    if (dtls) {
        // skip over Cookie and CookieLen
        if (parser_read_uint(&p, L_DTLSCookieLength, &tmp_len) == status_err) {
            return;
        }
        if (parser_skip(&p, tmp_len + L_DTLSCookieLength) == status_err) {
            return;
        }
    }

    // parse clientHello.Ciphersuites
    if (parser_read_and_skip_uint(&p, L_CipherSuiteVectorLength, &tmp_len)) {
        return;
    }
    ciphersuite_vector.parse(p, tmp_len);
    // degrease_octet_string(x->last_capture + 2, tmp_len);

    // parse compression methods
    if (parser_read_and_skip_uint(&p, L_CompressionMethodsLength, &tmp_len) == status_err) {
        return;
    }
    compression_methods.parse(p, tmp_len);

    // parse extensions vector
    if (parser_read_and_skip_uint(&p, L_ExtensionsVectorLength, &tmp_len)) {
        return;
    }
    extensions.parse_soft_fail(p, tmp_len);

    return;

}

void tls_client_hello::write_json(struct json_object &record, bool output_metadata) const {
    if (ciphersuite_vector.is_not_readable()) {
        return;
    }
    const char *label = "tls";
    if (dtls) {
        label = "dtls";
    }
    struct json_object tls{record, label};
    struct json_object tls_client{tls, "client"};
    if (output_metadata) {
        tls_client.print_key_hex("version", protocol_version);
        tls_client.print_key_hex("random", random);
        tls_client.print_key_hex("session_id", session_id);
        tls_client.print_key_hex("cipher_suites", ciphersuite_vector);
        tls_client.print_key_hex("compression_methods", compression_methods);
        //tls.print_key_hex("extensions", hello.extensions);
        //hello.extensions.print(tls, "extensions");
    }
    extensions.print_server_name(tls_client, "server_name");
    if (output_metadata) {
        extensions.print_session_ticket(tls_client, "session_ticket");
    }
    tls_client.close();
    tls.close();
}

// static function
//
void tls_client_hello::write_json(struct datum &data, struct json_object &record, bool output_metadata) {
    struct tls_record rec;
    rec.parse(data);
    struct tls_handshake handshake;
    handshake.parse(rec.fragment);
    struct tls_client_hello hello;
    hello.parse(handshake.body);
    hello.write_json(record, output_metadata);
}

void tls_client_hello::write_fingerprint(struct buffer_stream &buf) const {
    /*
     * copy clientHello.ProtocolVersion
     */
    buf.write_char('(');
    buf.raw_as_hex(protocol_version.data, protocol_version.length());
    buf.write_char(')');

    /* copy ciphersuite offer vector */
    buf.write_char('(');
    raw_as_hex_degrease(buf, ciphersuite_vector.data, ciphersuite_vector.length());
    buf.write_char(')');

    /*
     * copy extensions vector
     */
    buf.write_char('(');
    extensions.fingerprint(buf);
    buf.write_char(')');
}

void tls_client_hello::operator()(struct buffer_stream &buf) const {
    buf.write_char('\"');
    write_fingerprint(buf);
    buf.write_char('\"');
}


void tls_server_hello::parse(struct datum &p) {
    extractor_debug("%s: processing packet with %td bytes\n", __func__, p->data_end - p->data);

    parse_tls_server_hello(p);

    return;

}

enum status tls_server_hello::parse_tls_server_hello(struct datum &record) {
    size_t tmp_len;

    extractor_debug("%s: processing server_hello with %td bytes\n", __func__, record.data_end - record.data);

    protocol_version.parse(record, L_ProtocolVersion);
    random.parse(record, L_Random);

    /* skip over SessionID and SessionIDLen */
    if (parser_read_uint(&record, L_SessionIDLength, &tmp_len) == status_err) {
	    goto bail;
    }
    if (parser_skip(&record, tmp_len + L_SessionIDLength) == status_err) {
	    goto bail;
    }

    ciphersuite_vector.parse(record, L_CipherSuite);

    compression_method.parse(record, L_CompressionMethod);

    // parse extensions vector
    if (parser_read_and_skip_uint(&record, L_ExtensionsVectorLength, &tmp_len)) {
        return status_ok;  // could differentiate between err/ok
    }
    extensions.parse(record, tmp_len);

    return status_ok;

 bail:
    return status_err;
}

void tls_server_hello::operator()(struct buffer_stream &buf) const {
    buf.write_char('\"');
    /*
     * copy serverHello.ProtocolVersion
     */
    buf.write_char('(');
    buf.raw_as_hex(protocol_version.data, protocol_version.length());
    buf.write_char(')');

    /* copy ciphersuite offer vector */
    buf.write_char('(');
    buf.raw_as_hex(ciphersuite_vector.data, ciphersuite_vector.length());  /* TBD: do we need to degrease? */
    buf.write_char(')');

    /*
     * copy extensions vector
     */
    buf.write_char('(');
    extensions.fingerprint(buf);
    buf.write_char(')');
    buf.write_char('\"');
}

void tls_server_hello::write_json(struct json_object &o) const {
    o.print_key_hex("version", protocol_version);
    o.print_key_hex("random", random);
    //o.print_key_hex("session_id", session_id);
    //o.print_key_hex("cipher_suites", ciphersuite_vector);
    o.print_key_hex("compression_method", compression_method);
    //o.print_key_hex("extensions", hello.extensions);
    //hello.extensions.print(o, "extensions");
    extensions.print_server_name(o, "server_name");
    extensions.print_session_ticket(o, "session_ticket");
    o.print_key_value("fingerprint", *this); 
}

void tls_server_certificate::write_json(struct json_array &a, bool json_output) const {

    struct datum tmp_cert_list = certificate_list;
    while (parser_get_data_length(&tmp_cert_list) > 0) {

        /* get certificate length */
        size_t tmp_len;
        if (tmp_cert_list.read_uint(&tmp_len, L_CertificateLength) == false) {
            return;
        }

        if (tmp_len > (unsigned)parser_get_data_length(&tmp_cert_list)) {
            tmp_len = parser_get_data_length(&tmp_cert_list); /* truncate */
        }

        if (tmp_len == 0) {
            return; /* don't bother printing out a partial cert if it has a length of zero */
        }

        struct json_object o{a};
        if (json_output) {
            struct json_object_asn1 cert{o, "cert"};
            struct x509_cert c;
            c.parse(tmp_cert_list.data, tmp_len);
            c.print_as_json(cert, {}, NULL);
            cert.close();
        } else {
            struct datum cert_parser{tmp_cert_list.data, tmp_cert_list.data + tmp_len};
            o.print_key_base64("base64", cert_parser);
        }
        o.close();

        /*
         * advance parser over certificate data
         */
        if (parser_skip(&tmp_cert_list, tmp_len) == status_err) {
            return;
        }
    }
}
