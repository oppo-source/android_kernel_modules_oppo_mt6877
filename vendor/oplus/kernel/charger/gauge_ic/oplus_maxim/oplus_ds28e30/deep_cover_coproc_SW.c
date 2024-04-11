// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2024 Oplus. All rights reserved.
 */

/**  @file deep_cover_coproc.c
 *   @brief Coprocessor functions to support DS28C36/DS2476
 *     implemented in software.
 */
#define DEEP_COVER_COPROC
#include "deep_cover_coproc.h"
#include "ecdsa_generic_api.h"
#include "ucl_defs.h"
#include "ucl_sha256.h"
#include "ucl_sys.h"
#include "ucl_types.h"


/* Software compute functions */
int deep_cover_verify_ecdsa_signature(uchar *message, int msg_len, uchar *pubkey_x, uchar *pubkey_y,
									uchar *sig_r, uchar *sig_s);
int deep_cover_compute_ecdsa_signature(uchar *message, int msg_len, uchar *priv_key, uchar *sig_r,
									 uchar *sig_s);
int deep_cover_create_ecdsa_certificate(uchar *sig_r, uchar *sig_s,
                                      uchar *pub_x, uchar *pub_y,
                                      uchar *custom_cert_fields, int cert_len,
                                      uchar *priv_key);
int deep_cover_verify_ecdsa_certificate(uchar *sig_r, uchar *sig_s,
                                      uchar *pub_x, uchar *pub_y,
                                      uchar *custom_cert_fields, int cert_len,
                                      uchar *ver_pubkey_x, uchar *ver_pubkey_y);
int deep_cover_coproc_setup(int master_secret, int ecdsa_signing_key, int ecdh_key,
							int ecdsa_verify_key);

/* extenral debug  */
extern int dprintf(char *format, ...);

/* Deep Cover coprocessor functions (software implementation) */
int deep_cover_coproc_setup(int master_secret, int ecdsa_signing_key, int ecdh_key,
							int ecdsa_verify_key)
{
	/* initialize the FCL library */
	ucl_init();

	return TRUE;
}

/*  Helper function to verify ECDSA signature using the DS2476 public.
 @param[in] message
 Messge to hash for signature verification
 @param[in] msg_len
 Length of message in bytes
 @param[in] pubkey_x
 32-byte buffer container the public key x value
 @param[in] pubkey_y
 32-byte buffer container the public key y value
 @param[in] sig_r
 Signature r to verify
 @param[in] sig_s
 Signature s to verify
 @return
 TRUE - signature verified @n
 FALSE - signature not verified
 */
int deep_cover_verify_ecdsa_signature(uchar *message, int msg_len, uchar *pubkey_x, uchar *pubkey_y,
									uchar *sig_r, uchar *sig_s)
{
	int configuration;
	ucl_type_ecdsa_signature signature;
	ucl_type_ecc_u8_affine_point public_key;
	int rslt;

	/* Hook structure to r/s */
	signature.r = sig_r;
	signature.s = sig_s;

	/* Hook structure to x/y */
	public_key.x = pubkey_x;
	public_key.y = pubkey_y;

	/* construct configuration */
	configuration = (SECP256R1 << UCL_CURVE_SHIFT) ^ (UCL_MSG_INPUT << UCL_INPUT_SHIFT) ^
			(UCL_SHA256 << UCL_HASH_SHIFT);

	rslt = ucl_ecdsa_verification(public_key, signature, ucl_sha256,
			message, msg_len, &secp256r1, configuration);

	return (rslt == SUCCESS_FINISHED);
}

/*  Helper function to compute Signature using the specified private key.
 @param[in] message
 Messge to hash for signature verification
 @param[in] msg_len
 Length of message in bytes
 @param[in] priv_key (not used, private key must be either Private Key A, Private Key B, or Private Key C)
 32-byte buffer container the private key to use to compute signature
 @param[out] sig_r
 signature portion r
 @param[out] sig_s
 signature portion s
 @return
 TRUE - signature verified @n
 FALSE - signature not verified
 */
int deep_cover_compute_ecdsa_signature(uchar *message, int msg_len, uchar *priv_key,
									 uchar *sig_r, uchar *sig_s)
{
	int configuration;
	ucl_type_ecdsa_signature signature;

	/* hook up r/s to the signature structure */
	signature.r = sig_r;
	signature.s = sig_s;
	/* construct configuration */
	configuration = (SECP256R1 << UCL_CURVE_SHIFT) ^ (UCL_MSG_INPUT << UCL_INPUT_SHIFT) ^
		(UCL_SHA256 << UCL_HASH_SHIFT);
	/* create signature and return result */
	return (ucl_ecdsa_signature(signature, priv_key, ucl_sha256, message, msg_len,
				&secp256r1, configuration) == SUCCESS_FINISHED);
}

/* Create certificate to authorize the provided Public Key for writes.
 @param[out] sig_r
 Buffer for R portion of signature (MSByte first)
 @param[out] sig_s
 Buffer for S portion of signature (MSByte first)
 @param[in] pub_x
 Public Key x to create certificate
 @param[in] pub_y
 Public Key y to create certificate
 @param[in] custom_cert_fields
 Buffer for certificate customization fields (LSByte first)
 @param[in] cert_len
 Length of certificate customization field
 @param[in] priv_key (not used, Private Key A, Private Key B, or Private Key C)
 32-byte buffer containing private key used to sign certificate
 @return
 TRUE - certificate created @n
 FALSE - certificate not created
 */
int deep_cover_create_ecdsa_certificate(uchar *sig_r, uchar *sig_s,
                                      uchar *pub_x, uchar *pub_y,
                                      uchar *custom_cert_fields, int cert_len,
                                      uchar *priv_key)
{
	uchar message[MESSAGE_MAX_LEN];
	int  msg_len;

	/* build message to verify signature */
	/* Public Key X | Public Key Y | Buffer (custom fields) */
	/* Public Key X */
	msg_len = ZERO_VALUE;
	memcpy(&message[msg_len], pub_x, BYTE_LENGTH_32);
	msg_len += BYTE_LENGTH_32;
	/* Public Key SY */
	memcpy(&message[msg_len], pub_y, BYTE_LENGTH_32);
	msg_len += BYTE_LENGTH_32;
	/* Customization cert fields */
	memcpy(&message[msg_len], custom_cert_fields, cert_len);
	msg_len += cert_len;

	/* Compute the certificate */
	return deep_cover_compute_ecdsa_signature(message, msg_len, priv_key, sig_r, sig_s);
}

/* Verify certificate.
 @param[in] sig_r
 Buffer for R portion of certificate signature (MSByte first)
 @param[in] sig_s
 Buffer for S portion of certificate signature (MSByte first)
 @param[in] pub_x
 Public Key x to verify
 @param[in] pub_y
 Public Key y to verify
 @param[in] custom_cert_fields
 Buffer for certificate customization fields (LSByte first)
 @param[in] cert_len
 Length of certificate customization field
 @param[in] ver_pubkey_x
 32-byte buffer container the verify public key x
 @param[in] ver_pubkey_y
 32-byte buffer container the verify public key y
  @return
  TRUE - certificate valid @n
  FALSE - certificate not valid */
int deep_cover_verify_ecdsa_certificate(uchar *sig_r, uchar *sig_s,
				uchar *pub_x, uchar *pub_y,
				uchar *custom_cert_fields, int cert_len,
				uchar *ver_pubkey_x, uchar *ver_pubkey_y)
{
	uchar message[MESSAGE_MAX_LEN];
	int  msg_len;

	/* build message to verify signature */
	/* Public Key X | Public Key Y | Buffer (custom fields) */
	/* Public Key X */
	msg_len = ZERO_VALUE;
	memcpy(&message[msg_len], pub_x, BYTE_LENGTH_32);
	msg_len += BYTE_LENGTH_32;
	/* Public Key SY */
	memcpy(&message[msg_len], pub_y, BYTE_LENGTH_32);
	msg_len += BYTE_LENGTH_32;
	/* Customization cert fields */
	memcpy(&message[msg_len], custom_cert_fields, cert_len);
	msg_len += cert_len;

	/* Compute the certificate */
	return deep_cover_verify_ecdsa_signature(message, msg_len, ver_pubkey_x, ver_pubkey_y,
			sig_r, sig_s);
}
