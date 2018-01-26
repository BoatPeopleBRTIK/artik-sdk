/*
 *
 * Copyright 2017 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 */

#include <artik_security.h>
#include "os_security.h"

static artik_error request(artik_security_handle *handle);
static artik_error release(artik_security_handle handle);
static artik_error get_certificate(artik_security_handle handle,
		artik_security_certificate_id cert_id, char **cert);
static artik_error get_key_from_cert(artik_security_handle handle,
		const char *cert, char **key);
static artik_error get_ca_chain(artik_security_handle handle,
		artik_security_certificate_id cert_id, char **chain);
static artik_error get_random_bytes(artik_security_handle handle,
		unsigned char *rand, int len);
static artik_error get_certificate_sn(artik_security_handle handle, artik_security_certificate_id cert_id,
		unsigned char *sn, unsigned int *len);
static artik_error get_ec_pubkey_from_cert(const char *cert, char **key);
static artik_error verify_signature_init(artik_security_handle *handle,
		const char *signature_pem, const char *root_ca,
		const artik_time *signing_time_in, artik_time *signing_time_out);
static artik_error verify_signature_update(artik_security_handle handle,
		unsigned char *data, unsigned int data_len);
static artik_error verify_signature_final(artik_security_handle handle);
static artik_error convert_pem_to_der(const char *pem_data,
		unsigned char **der_data);

EXPORT_API const artik_security_module security_module = {
	request,
	release,
	get_certificate,
	get_key_from_cert,
	get_ca_chain,
	get_random_bytes,
	get_certificate_sn,
	get_ec_pubkey_from_cert,
	verify_signature_init,
	verify_signature_update,
	verify_signature_final,
	convert_pem_to_der
};

artik_error request(artik_security_handle *handle)
{
	return os_security_request(handle);
}

artik_error release(artik_security_handle handle)
{
	return os_security_release(handle);
}

artik_error get_certificate(artik_security_handle handle,
		artik_security_certificate_id cert_id, char **cert)
{
	return os_security_get_certificate(handle, cert_id, cert);
}

artik_error get_key_from_cert(artik_security_handle handle, const char *cert,
			      char **key)
{
	return os_security_get_key_from_cert(handle, cert, key);
}

artik_error get_ca_chain(artik_security_handle handle,
		artik_security_certificate_id cert_id, char **chain)
{
	return os_security_get_ca_chain(handle, cert_id, chain);
}

artik_error get_random_bytes(artik_security_handle handle, unsigned char *rand,
			     int len)
{
	return os_get_random_bytes(handle, rand, len);
}

artik_error get_certificate_sn(artik_security_handle handle,
		artik_security_certificate_id cert_id, unsigned char *sn, unsigned int *len)
{
	return os_get_certificate_sn(handle, cert_id, sn, len);
}

artik_error get_ec_pubkey_from_cert(const char *cert, char **key)
{
	return os_get_ec_pubkey_from_cert(cert, key);
}

artik_error verify_signature_init(artik_security_handle *handle,
		const char *signature_pem, const char *root_ca,
		const artik_time *signing_time_in, artik_time *signing_time_out)
{
	return os_verify_signature_init(handle, signature_pem, root_ca,
			signing_time_in, signing_time_out);
}

artik_error verify_signature_update(artik_security_handle handle,
		unsigned char *data, unsigned int data_len)
{
	return os_verify_signature_update(handle, data, data_len);
}

artik_error verify_signature_final(artik_security_handle handle)
{
	return os_verify_signature_final(handle);
}

artik_error convert_pem_to_der(const char *pem_data, unsigned char **der_data)
{
	return os_convert_pem_to_der(pem_data, der_data);
}
