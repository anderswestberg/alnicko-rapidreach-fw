/**
 * @file ca_certificate.h
 * @brief Certificate storage used for HTTPS client authentication.
 *
 * This file contains an array of trusted CA root certificates used for
 * establishing secure HTTPS (TLS) connections. 
 *
 * HTTPS support in this module is enabled via the `RPR_ENABLE_HTTPS` Kconfig flag.
 * When enabled, it also selects `MBEDTLS_PEM_CERTIFICATE_FORMAT` to support PEM-encoded
 * certificates. If your certificates are in DER format, you may disable PEM support.
 * 
 * @author Eugene K.
 * @copyright (C) 2025 Alnicko Lab OU. All rights reserved.
 */

#ifndef __CA_CERTIFICATE_H__
#define __CA_CERTIFICATE_H__

#define CA_CERTIFICATE_TAG 1

/**
 * By default only one certificate — **AmazonRootCA1**, which is required to work
 * with the **httpbin.org** server.
 * 
 * To add more certificates:
 * - Append them to the `ca_certificates` array below using a comma separator.
 * - Ensure each certificate is included as a valid `unsigned char[]` block.
 */

static const unsigned char *ca_certificates[] = {
#include "amazonrootca1.pem"
};

#endif /* __CA_CERTIFICATE_H__ */