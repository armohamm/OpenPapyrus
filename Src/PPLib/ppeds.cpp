// PPEDS.CPP
// ������ � ���
//
#include <pp.h>
#pragma hdrstop

#define MY_ENCODING_TYPE		(PKCS_7_ASN_ENCODING | X509_ASN_ENCODING)
#define CERTIFICATE_STORE_NAME	"My"
#define MY_PROV_TYPE			75 // ��� ���������� PROV_GOST_2001_DH
#define MAX_NAME_LEN			256
#define SIZE_TO_READ			1024

// ��� ��������� ������ ���� ������� � ������� ��������� ��������. ���� ������ ������� ���.
#define HOST					"tsptest.e-notary.ru"
#define URL						"http://tsptest.e-notary.ru"

//void ShowError(const char * pErrorMsg)
//{
//	SString msg;
//	DWORD err_code = GetLastError();
//	msg.Z().Cat(pErrorMsg).Cat("��� ������ ").Cat(err_code);
//	PPSetError(PPERR_EDSERROR, msg.ToOem());
//}

void PPEds::GetEncryptedFileName(const char * pFileName, SString & rEncryptFileName, int sameFile/* = 1*/)
{
	int count = 0;
	SString ext;
	rEncryptFileName.Z().Cat(pFileName);
	ext.Z();
	while(rEncryptFileName.Last() != '.') {
		count++;
		rEncryptFileName.TrimRight();
	}
	rEncryptFileName.Z().Cat(pFileName);
	rEncryptFileName.Sub(rEncryptFileName.Len() - count, count, ext);
    rEncryptFileName.Z().CopyFromN(pFileName, sstrlen(pFileName) - count);
	if(sameFile)
		rEncryptFileName.Cat("_1").Dot().Cat(ext);
	else
		rEncryptFileName.Cat("_encoded").Dot().Cat(ext);
}

void PPEds::GetSignFileName(const char * pDocName, SString & rSignFileName)
{
	rSignFileName.Z().Cat(pDocName);
	while(rSignFileName.Last() != '.')
		rSignFileName.TrimRight();
	rSignFileName.Cat("p7s");
	//rSignFileName.Cat("sgn");
}

int PPEds::GetCert(HCERTSTORE & rCertSore, PCCERT_CONTEXT & rCert, const char * pOwnerName)
{
	int    ok = 1;
	THROW_PP((rCertSore = CertOpenSystemStore(0, CERTIFICATE_STORE_NAME)), PPERR_EDS_OPENCERTSTORE); // @unicodeproblem
	// CERT_FIND_SUBJECT_STR_A ��� ��������
	// CERT_FIND_SUBJECT_STR ��� ���������
	// ���� ��� CERT_FIND_SUBJECT_STR_W
	// ����, ��� ���������, CERT_FIND_SUBJECT_STR_A �������� � � ���������
   THROW_PP((rCert = CertFindCertificateInStore(rCertSore, MY_ENCODING_TYPE, 0,
       CERT_FIND_SUBJECT_STR_A, pOwnerName, NULL)), PPERR_EDS_GETCERT);
	CATCHZOK;
	return ok;
}

// ����������� ��������� � ������� (��������, ��� ����� �����)
int PPEds::GetCert(PCCERT_CONTEXT & rCert)
{
	int ok = 1;
	// ��������� ����������� ��������� ������������������ ����������.
	HCRYPTPROV prov = 0;
    THROW_PP(CryptAcquireContext(&prov, NULL, NULL, MY_PROV_TYPE, NULL), PPERR_EDS_GETPROVIDER);
    // ��������� ����������������� ����� (�� ���� ����� ������)
    DWORD key_type = AT_SIGNATURE;
	HCRYPTKEY h_key = 0;
    if(!CryptGetUserKey(prov, key_type, &h_key)) {
        key_type = AT_KEYEXCHANGE;
        THROW_PP(CryptGetUserKey(prov, key_type, &h_key), PPERR_EDS_GETKEYFAILED);
    }
	DWORD user_cert_length = 0;
    // ����������� ������� ����������������� �����������.
    THROW_PP(CryptGetKeyParam(h_key, KP_CERTIFICATE, NULL, &user_cert_length, 0), PPERR_EDS_GETCERT);
	BYTE * p_user_cert = 0;
    // ������������� ������ ��� ����� ����������������� �����������.
    THROW_MEM((p_user_cert = (BYTE *)SAlloc::M(user_cert_length)));
    // ��������� ����������������� �����������.
	THROW_PP(CryptGetKeyParam(h_key, KP_CERTIFICATE, p_user_cert, &user_cert_length, 0), PPERR_EDS_GETCERT);
    // ������������ ��������� �����������.
    THROW_PP((rCert = CertCreateCertificateContext(MY_ENCODING_TYPE, p_user_cert, user_cert_length)), PPERR_EDS_GETCERT);
	CATCHZOK;
	if(prov) {
        CryptReleaseContext(prov, 0);
        prov = NULL;
    }
	return ok;
}

int PPEds::GetCryptoProv(PCCERT_CONTEXT & cert, HCRYPTPROV & rCryptoProv, DWORD & rKeySpec)
{
	int ok = 1;
	THROW_PP(CryptAcquireCertificatePrivateKey(cert, 0, NULL, &rCryptoProv,
		&rKeySpec, NULL), PPERR_EDS_GETPROVIDER);
	CATCHZOK;
	return ok;
}

int PPEds::CheckCertChain(PCCERT_CONTEXT & cert)
{
	int ok = 1;
	SString status_msg;
	HCERTCHAINENGINE h_chain_engine = NULL;
	CERT_CHAIN_ENGINE_CONFIG chain_config;
	PCCERT_CHAIN_CONTEXT p_chain_context = NULL;
	CERT_ENHKEY_USAGE enhkey_usage;
	CERT_USAGE_MATCH cert_usage;
	CERT_CHAIN_PARA chain_para;

	enhkey_usage.cUsageIdentifier = 0;
	enhkey_usage.rgpszUsageIdentifier = NULL;
	cert_usage.dwType = USAGE_MATCH_TYPE_AND;
	cert_usage.Usage = enhkey_usage;
	chain_para.cbSize = sizeof(CERT_CHAIN_PARA);
	chain_para.RequestedUsage = cert_usage;

	memzero(&chain_config, sizeof(CERT_CHAIN_ENGINE_CONFIG));
	chain_config.cbSize = sizeof(CERT_CHAIN_ENGINE_CONFIG);
	chain_config.dwFlags = CERT_CHAIN_REVOCATION_CHECK_CHAIN;

	// Create the nondefault certificate chain engine.
	THROW_PP(CertCreateCertificateChainEngine(&chain_config, &h_chain_engine), PPERR_EDS_CHAINCREAT);
	THROW_PP(CertGetCertificateChain(
		NULL, // Use the default chain engine.
		cert, // Pointer to the end certificate.
		NULL, // Use the default time.
		NULL, // Search no additional stores.
		&chain_para, // Use AND logic, and enhanced key usage
		// as indicated in the chain_para data structure.
		CERT_CHAIN_REVOCATION_CHECK_CHAIN,
		NULL, // Currently reserved.
		&p_chain_context), PPERR_EDS_CHAINCREAT) // Return a pointer to the chain created.
	if(p_chain_context->TrustStatus.dwErrorStatus != 0) {
		THROW_PP(p_chain_context->TrustStatus.dwErrorStatus != CERT_TRUST_IS_NOT_TIME_VALID, PPERR_EDS_CERTTRUSTTIMENOTVALID);
		THROW_PP(p_chain_context->TrustStatus.dwErrorStatus != CERT_TRUST_IS_REVOKED, PPERR_EDS_CERTTRUSTISREVOKED);
		THROW_PP(p_chain_context->TrustStatus.dwErrorStatus != CERT_TRUST_IS_NOT_SIGNATURE_VALID, PPERR_EDS_CERTTRUSTSIGNNOTVALID);
		THROW_PP(p_chain_context->TrustStatus.dwErrorStatus != CERT_TRUST_IS_NOT_VALID_FOR_USAGE, PPERR_EDS_CERTTRUSTUSAGENOTVALID);
		THROW_PP(p_chain_context->TrustStatus.dwErrorStatus != CERT_TRUST_IS_UNTRUSTED_ROOT, PPERR_EDS_CERTTRUSUNTRUSTEDROOT);
		THROW_PP(p_chain_context->TrustStatus.dwErrorStatus != CERT_TRUST_REVOCATION_STATUS_UNKNOWN, PPERR_EDS_CERTTRUSTREVOKTNUNKNOWN);
		THROW_PP(p_chain_context->TrustStatus.dwErrorStatus != CERT_TRUST_IS_CYCLIC, PPERR_EDS_CERTTRUSTCYCLIC);
/*!!!*/	THROW_PP(p_chain_context->TrustStatus.dwErrorStatus != CERT_TRUST_INVALID_EXTENSION, PPERR_EDS_CERTTRUSTINVEXT);
/*!!!*/ THROW_PP(p_chain_context->TrustStatus.dwErrorStatus != CERT_TRUST_INVALID_POLICY_CONSTRAINTS, PPERR_EDS_CERTTRUSTPOLICYINV);
/*!!!*/	THROW_PP(p_chain_context->TrustStatus.dwErrorStatus != CERT_TRUST_INVALID_BASIC_CONSTRAINTS, PPERR_EDS_CERTTRUSTBASICCONSTRINV);
		THROW_PP(p_chain_context->TrustStatus.dwErrorStatus != CERT_TRUST_IS_PARTIAL_CHAIN, PPERR_EDS_CERTTRUSTCHAINISPARTNL);
		THROW_PP(p_chain_context->TrustStatus.dwErrorStatus != CERT_TRUST_CTL_IS_NOT_TIME_VALID, PPERR_EDS_CERTTRUSTCTLTIMENOTVALID);
		THROW_PP(p_chain_context->TrustStatus.dwErrorStatus != CERT_TRUST_CTL_IS_NOT_SIGNATURE_VALID, PPERR_EDS_CERTTRUSTCTLSIGNNOTVALID);
		THROW_PP(p_chain_context->TrustStatus.dwErrorStatus != CERT_TRUST_CTL_IS_NOT_VALID_FOR_USAGE, PPERR_EDS_CERTTRUSTCTLUSAGENOTVALID);
	}
	if(p_chain_context->TrustStatus.dwInfoStatus != 0) {
		switch(p_chain_context->TrustStatus.dwInfoStatus) {
			case CERT_TRUST_HAS_EXACT_MATCH_ISSUER:
				PPOutputMessage("An exact match issuer certificate has been found for this certificate", mfYes | mfLargeBox);
				break;
			case CERT_TRUST_HAS_KEY_MATCH_ISSUER:
				PPOutputMessage("A key match issuer certificate has been found for this certificate", mfYes | mfLargeBox);
				break;
			case CERT_TRUST_HAS_NAME_MATCH_ISSUER:
				PPOutputMessage("A name match issuer certificate has been found for this certificate", mfYes | mfLargeBox);
				break;
			case CERT_TRUST_IS_SELF_SIGNED:
				PPOutputMessage("This certificate is self-signed", mfYes | mfLargeBox);
				break;
			case CERT_TRUST_HAS_PREFERRED_ISSUER:
				PPOutputMessage("The certificate or chain has a preferred issuer", mfYes | mfLargeBox);
				break;
			case CERT_TRUST_IS_COMPLEX_CHAIN:
				PPOutputMessage("The certificate chain created is a complex chain", mfYes | mfLargeBox);
				break;
		}
	}
	CATCHZOK;
	CertFreeCertificateChainEngine(h_chain_engine);
	CertFreeCertificateChain(p_chain_context);
	return ok;
}

int PPEds::EncodeData(const char * pOwnerName, const char * pFileName, int sameFile /*= 1*/)
{
	int    ok = 1;
	HCERTSTORE cert_sore = 0;
	HCRYPTPROV prov = 0;
    PCCERT_CONTEXT p_owner_cert = 0;
	BYTE * pb_indata = NULL;
	uint  blob_count = 0;
	DWORD cb_indata = 0;
	DWORD buf_len = 0;
	DWORD block_len = 0;
	DWORD l_block_len = 0;
	DWORD key_length = 0;
	DWORD block_size_to_read = 0;
	HCRYPTKEY h_pub_key = 0;
	SString encode_file_name;
	SFile file, encode_file;
	int64 file_size = 0;

	// ������� ����������
	THROW(GetCert(cert_sore, p_owner_cert, pOwnerName));

	// ������� ����������
	DWORD dw_key_spec = 0;
	THROW(GetCryptoProv(p_owner_cert, prov, dw_key_spec));

	// ������� �������� ���� ����������
	THROW_PP(CryptImportPublicKeyInfo(prov, MY_ENCODING_TYPE, &p_owner_cert->pCertInfo->SubjectPublicKeyInfo, &h_pub_key), PPERR_EDS_GETKEYFAILED);

	// ������� ����� �����
	THROW_PP((key_length = CertGetPublicKeyLength(MY_ENCODING_TYPE, &p_owner_cert->pCertInfo->SubjectPublicKeyInfo)), PPERR_EDS_GETKEYLENFAILED);
	key_length /= 8;

	// ������� ������ ����� ������ ��� ����������
	l_block_len = sizeof(block_len);
	THROW_PP(CryptGetKeyParam(h_pub_key, KP_BLOCKLEN, (PBYTE)&block_len, &l_block_len, 0), PPERR_EDS_GETBLOCKSIZEFAILED);
	block_len /= 8;
	if(block_len > (key_length - 11))
		block_size_to_read = key_length - 11;
	else
		block_size_to_read = block_len;

	THROW_SL(file.Open(pFileName, SFile::mRead|SFile::mBinary));
	GetEncryptedFileName(pFileName, encode_file_name, sameFile);
	THROW_SL(encode_file.Open(encode_file_name, SFile::mWrite | SFile::mBinary));
	file.CalcSize(&file_size);
	blob_count = (int)ceil((double)file_size / block_size_to_read);
	// ������� ������ ������������� ������
	buf_len = block_size_to_read;
	cb_indata = block_size_to_read;
	THROW_PP(CryptEncrypt(h_pub_key, NULL, FALSE, 0, NULL, &buf_len, cb_indata), PPERR_EDS_ENCRYPTIONFAILED);

	// ������� ������
	THROW_MEM(pb_indata = new BYTE[buf_len]);
	// ������� ����� ������ ��� ���������� �����
	for(size_t i = 1; i < (size_t)blob_count; i++, file_size -= block_size_to_read) {
		memzero(pb_indata, buf_len);
		file.ReadV(pb_indata, block_size_to_read);
		cb_indata = block_size_to_read;
		// ������� ������
		THROW_PP(CryptEncrypt(h_pub_key, NULL, FALSE, 0, pb_indata, &cb_indata, buf_len), PPERR_EDS_ENCRYPTIONFAILED);
		// ������� � ����
		encode_file.Write(pb_indata, buf_len);
	}
	// ������ �������� � ��������� ������
	block_size_to_read = (DWORD)file_size;
	ZDELETE(pb_indata);
	cb_indata = block_size_to_read;
	buf_len = block_size_to_read;
	// ������� ������ ������������� ������
	THROW_PP(CryptEncrypt(h_pub_key, NULL, FALSE, 0, NULL, &buf_len, cb_indata), PPERR_EDS_ENCRYPTIONFAILED);
	THROW_MEM(pb_indata = new BYTE[buf_len]);
	memzero(pb_indata, buf_len);
	file.ReadV(pb_indata, block_size_to_read);
	// ������� ������
	THROW_PP(CryptEncrypt(h_pub_key, NULL, TRUE, 0, pb_indata, &cb_indata, buf_len), PPERR_EDS_ENCRYPTIONFAILED);
	encode_file.Write(pb_indata, buf_len);

	file.Close();
	encode_file.Close();
	if(sameFile) {
		file.Remove(pFileName);
		file.Rename(encode_file_name, pFileName);
	}

	CATCHZOK;
	ZDELETE(pb_indata);
	if(p_owner_cert) {
		CertFreeCertificateContext(p_owner_cert);
		p_owner_cert = 0;
	}
	if(h_pub_key)
		CryptDestroyKey(h_pub_key);
	if(prov) {
        CryptReleaseContext(prov, 0);
        prov = NULL;
    }
	if(cert_sore)
		CertCloseStore(cert_sore, 0);
	return ok;
}

int PPEds::DecodeData(const char * pOwnerName, const char * pFileName)
{
	int    ok = 1;
	HCERTSTORE cert_sore = 0;
    PCCERT_CONTEXT p_owner_cert = 0;
	HCRYPTPROV prov = NULL;
	HCRYPTKEY h_private_key = 0;
	BYTE * pb_indata = NULL;
	DWORD cb_indata = 0;
	int64 file_size = 0;
	DWORD block_len = 0;
	DWORD l_block_len = 0;
	uint  blob_count = 0;
	SString decode_file_name;

	SFile file, decode_file;

	// ������� ��������� ������������
	THROW(GetCert(cert_sore, p_owner_cert, pOwnerName));

	DWORD dw_key_spec;
	THROW(GetCryptoProv(p_owner_cert, prov, dw_key_spec));

	// ������� �������� ����
	THROW_PP(CryptGetUserKey(prov, dw_key_spec, &h_private_key), PPERR_EDS_GETKEYFAILED);

	GetEncryptedFileName(pFileName, decode_file_name);
	THROW_SL(decode_file.Open(decode_file_name, SFile::mWrite | SFile::mBinary));
	THROW_SL(file.Open(pFileName, SFile::mRead | SFile::mBinary));
	file.CalcSize(&file_size);

	// ������ ����� ����� ��� ������ ������ (� �����)
	l_block_len = sizeof(block_len);
	THROW_PP(CryptGetKeyParam(h_private_key, KP_BLOCKLEN, (PBYTE)&block_len, &l_block_len, 0), PPERR_EDS_GETBLOCKSIZEFAILED);
	block_len /= 8;
	blob_count = (int)ceil((double)file_size / block_len);
	for(size_t i = 0; i < (size_t)blob_count; i++, file_size -= block_len) {
		cb_indata = block_len;
		if(file_size < block_len)
			cb_indata = (DWORD)file_size;
		// ������� ������
		THROW_MEM(pb_indata = new BYTE[cb_indata]);
		memzero(pb_indata, cb_indata);
		file.ReadV(pb_indata, cb_indata);
		// �������������� ������
		THROW_PP(CryptDecrypt(h_private_key, 0, (file_size > block_len) ? FALSE : TRUE, 0,
			pb_indata, &cb_indata), PPERR_EDS_DECRYPTIONFAILED);
		decode_file.Write(pb_indata, cb_indata);
		if(pb_indata)
			ZDELETE(pb_indata);
	}

	file.Close();
	decode_file.Close();
	file.Remove(pFileName);
	file.Rename(decode_file_name, pFileName);

	CATCHZOK;
	if(pb_indata)
		ZDELETE(pb_indata);
	if(prov) {
        CryptReleaseContext(prov, 0);
        prov = NULL;
    }
	if(h_private_key)
		CryptDestroyKey(h_private_key);
	if(p_owner_cert) {
		CertFreeCertificateContext(p_owner_cert);
		p_owner_cert = 0;
	}
	if(cert_sore)
		CertCloseStore(cert_sore, 0);
	return ok;
}

int PPEds::FirstSignData(const char * pSignerName, const char * pFileName, SString & rSignFileName)
{
	int ok = 1, blob_count = 0;
	int64 file_size = 0;
    BYTE * pb_indata = NULL;
    DWORD cb_indata = 0;
	HCERTSTORE cert_sore = 0;
    PCCERT_CONTEXT p_signer_cert = 0;
    CRYPT_SIGN_MESSAGE_PARA  sig_params;
    DWORD cb_signed_message_blob = 0;
    BYTE * pb_signed_message_blob = NULL;
	const BYTE ** message_array = 0;
	DWORD * message_size_array = 0;
	SString sign_file_name;
	SFile file;

	// ������� ����������
	THROW(GetCert(cert_sore, p_signer_cert, pSignerName));

	// �������� ������� ������������
	THROW(CheckCertChain(p_signer_cert));

    // ������-�:)
    // Initialize the signature structure
	memzero(&sig_params, sizeof(CRYPT_SIGN_MESSAGE_PARA));
    sig_params.cbSize = sizeof(CRYPT_SIGN_MESSAGE_PARA);
    sig_params.dwMsgEncodingType = MY_ENCODING_TYPE;
    sig_params.pSigningCert = p_signer_cert;
	memzero(&sig_params.HashAlgorithm, sizeof(CRYPT_ALGORITHM_IDENTIFIER));
    sig_params.HashAlgorithm.pszObjId = p_signer_cert->pCertInfo->SignatureAlgorithm.pszObjId;
    sig_params.cMsgCert = 1;
    sig_params.rgpMsgCert = &p_signer_cert;

	// ����� ���� ����� ������ �� ���������� ������, �������� �����������
	THROW(CashOn(p_signer_cert));
	// ����������� ���� �������
	THROW_SL(file.Open(pFileName, SFile::mRead | SFile::mBinary));
	file.CalcSize(&file_size);
	blob_count = (int)ceil((double)file_size / SIZE_TO_READ);
	THROW_MEM(message_array = new const BYTE*[blob_count]);
	THROW_MEM(message_size_array = new DWORD[blob_count]);
	memzero(message_size_array, blob_count);
	for(size_t i = 0; i < (size_t)blob_count; i++, file_size -= SIZE_TO_READ) {
		if(file_size > SIZE_TO_READ)
			cb_indata = (DWORD)SIZE_TO_READ;
		else
			cb_indata = (DWORD)file_size;
		THROW_MEM(pb_indata = new BYTE[cb_indata]);
		memzero(pb_indata, cb_indata);
		file.ReadV(pb_indata, cb_indata);
		THROW_MEM(message_array[i] = new BYTE[cb_indata]);
		memzero((BYTE*)message_array[i], cb_indata);
		message_size_array[i] = cb_indata;
		memcpy((BYTE*)message_array[i], pb_indata, cb_indata);
		if(pb_indata)
			ZDELETE(pb_indata);
	}
	file.Close();

	// ������� ������ �������������� � ������������ �����
	if(!CryptSignMessage(&sig_params, TRUE /*detached*/, blob_count, message_array, message_size_array,
		NULL, &cb_signed_message_blob)) {
		THROW_PP(GetLastError() == SCARD_W_CANCELLED_BY_USER, PPERR_EDS_CREATESIGNATUREFAILED);
		ok = -1;
	}
	else {
		// �������� ������ ��� ������������ �����
		THROW_MEM(pb_signed_message_blob = new BYTE[cb_signed_message_blob]);
		memzero(pb_signed_message_blob, cb_signed_message_blob);

		// ���������� ����
		if(!CryptSignMessage(&sig_params, TRUE /*detached*/, blob_count, message_array, message_size_array,
			pb_signed_message_blob, &cb_signed_message_blob)) {
			// � pb_signed_message_blob ������ ��������� ����������� ���������
			THROW_PP(GetLastError() == SCARD_W_CANCELLED_BY_USER, PPERR_EDS_CREATESIGNATUREFAILED);
			ok = -1;
		}

		// �������� ������� � ����� ����
		if(!rSignFileName.NotEmpty()) {
			GetSignFileName(pFileName, sign_file_name);
			rSignFileName.CopyFrom(sign_file_name);
		}
		// ��� ����������� ������������
		else
			(sign_file_name = 0).CopyFrom(rSignFileName);
		THROW_SL(file.Open(sign_file_name, SFile::mWrite | SFile::mBinary));
		file.Write(pb_signed_message_blob, cb_signed_message_blob);
		file.Close();
	}

	CATCHZOK;
	if(pb_signed_message_blob)
		ZDELETE(pb_signed_message_blob);
	if(pb_indata)
		ZDELETE(pb_indata);
	if(p_signer_cert) {
		CashOff(p_signer_cert); // ��������� �����������
		CertFreeCertificateContext(p_signer_cert);
		p_signer_cert = 0;
	}
	if(cert_sore)
		CertCloseStore(cert_sore, 0);
	for(size_t i = 0; i < (size_t)blob_count; i++) {
		if(message_array)
			if(message_array[i])
				ZDELETE(message_array[i]);
	}
	if(message_array)
		ZDELETE(message_array);
	if(message_size_array)
		ZDELETE(message_size_array);
	return ok;
}

int PPEds::CoSignData(const char * pCosignerName, const char * pFileName, const char * pSignFileName)
{
	int ok = 1, blob_count = 0;
	HCERTSTORE cert_sore = 0;
	PCCERT_CONTEXT p_cosigner_cert = 0;
    HCRYPTPROV prov = NULL;
    HCRYPTMSG h_msg = NULL;
	BYTE * pb_sign_data = NULL; // ������ �� ����� �������� �� ���������� ����� �������
	DWORD cb_sign_data = 0;
    BYTE * pb_cosigned_data = NULL; // ������ � ����������� �������� ��� ������ � ���� ��������
    DWORD cb_cosigned_data = 0;
	BYTE * pb_indata = NULL; // ������ �� ���������
	DWORD cb_indata = 0;
	int64 file_size = 0;

	// ��������� ���� � ���������
	SFile file;
	THROW_SL(file.Open(pSignFileName, SFile::mRead | SFile::mBinary));
	file.CalcSize(&file_size);
	cb_sign_data = (DWORD)file_size;
	THROW_MEM(pb_sign_data = new BYTE[cb_sign_data]);
	memzero(pb_sign_data, cb_sign_data);
	file.ReadV(pb_sign_data, cb_sign_data);
	file.Close();

	// ������� ����������
	THROW(GetCert(cert_sore, p_cosigner_cert, pCosignerName));

	// �������� ����������������
	DWORD dw_key_spec;
	THROW(GetCryptoProv(p_cosigner_cert, prov, dw_key_spec));

	// �������� ������� ������������
	THROW(CheckCertChain(p_cosigner_cert));

    // ������� ������ � ��������� ��� ��������������
    THROW_PP((h_msg = CryptMsgOpenToDecode(MY_ENCODING_TYPE, CMSG_DETACHED_FLAG, 0, NULL, NULL, NULL)), PPERR_EDS_MSGOPENFAILED);

    // ��������� ������ � ������� � h_msg
	THROW_PP(CryptMsgUpdate(h_msg, pb_sign_data, cb_sign_data, TRUE), PPERR_EDS_MSGUPDATEFAILED);

	THROW_SL(file.Open(pFileName, SFile::mRead | SFile::mBinary));
	file_size = 0;
	file.CalcSize(&file_size);
	cb_indata = (DWORD)file_size;
	THROW_MEM(pb_indata = new BYTE[SIZE_TO_READ]);
	size_t actual_size = 0;
	blob_count = (int)ceil((double)file_size / SIZE_TO_READ);
	for(size_t i = 0; i < (size_t)blob_count; i++) {
		file.Read(pb_indata, SIZE_TO_READ, &actual_size);
		// ������� ������ � h_msg
		THROW_PP(CryptMsgUpdate(h_msg, pb_indata, (DWORD)actual_size, (i == blob_count - 1) ? TRUE : FALSE), PPERR_EDS_MSGUPDATEFAILED);
	}
	file.Close();

    // ��������� ��������� � ����������� � ��-����������
    CMSG_SIGNER_ENCODE_INFO cosigner_info;
    memzero(&cosigner_info, sizeof(CMSG_SIGNER_ENCODE_INFO));
    cosigner_info.cbSize = sizeof(CMSG_SIGNER_ENCODE_INFO);
    cosigner_info.pCertInfo = p_cosigner_cert->pCertInfo;
    cosigner_info.hCryptProv = prov;
    cosigner_info.dwKeySpec = dw_key_spec;
    cosigner_info.HashAlgorithm.pszObjId = p_cosigner_cert->pCertInfo->SignatureAlgorithm.pszObjId;

	// ��������� ���� � ��-���������� � ��� ������� � ���������
    if(!CryptMsgControl(h_msg, 0, CMSG_CTRL_ADD_SIGNER, &cosigner_info)) {
		THROW_PP(GetLastError() == SCARD_W_CANCELLED_BY_USER, PPERR_EDS_ADDSIGNERFAILED);
		ok = -1;
    }
	else {
		// ��������� ���������� ��-���������� � ���������
		CERT_BLOB cosign_�ert_blob;
		cosign_�ert_blob.cbData = p_cosigner_cert->cbCertEncoded;
		cosign_�ert_blob.pbData = p_cosigner_cert->pbCertEncoded;
		THROW_PP(CryptMsgControl(h_msg, 0, CMSG_CTRL_ADD_CERT, &cosign_�ert_blob), PPERR_EDS_ADDCERTFAILED);
		// �������� ������ ��-������������ �����
		THROW_PP(CryptMsgGetParam(h_msg, CMSG_ENCODED_MESSAGE, 0, NULL, &cb_cosigned_data), PPERR_EDS_CREATESIGNATUREFAILED);

		// �������� ������ ��� ��-������������ ���������
		THROW_MEM(pb_cosigned_data = new BYTE[cb_cosigned_data]);
		memzero(pb_cosigned_data, cb_cosigned_data);

		// �������� ��-����������� ����.
		THROW_PP(CryptMsgGetParam(h_msg, CMSG_ENCODED_MESSAGE, 0, pb_cosigned_data, &cb_cosigned_data), PPERR_EDS_CREATESIGNATUREFAILED);

		THROW_SL(file.Open(pSignFileName, SFile::mWrite | SFile::mBinary));
		file.Write(pb_cosigned_data, cb_cosigned_data);
		file.Close();
	}

	CATCHZOK;
	if(h_msg)
        CryptMsgClose(h_msg);
	if(prov) {
        CryptReleaseContext(prov, 0);
        prov = NULL;
    }
	if(pb_cosigned_data)
		ZDELETE(pb_cosigned_data);
	if(pb_indata)
		ZDELETE(pb_indata);
	if(pb_sign_data)
		ZDELETE(pb_sign_data);
	if(p_cosigner_cert) {
		CertFreeCertificateContext(p_cosigner_cert);
		p_cosigner_cert = 0;
	}
	if(cert_sore)
		CertCloseStore(cert_sore, 0);
	return ok;
}

int PPEds::CountersignData(const char * pCountersignerName, int signerNumber, const char * pFileName, const char * pSignFileName)
{
	int ok = 1;
	HCERTSTORE cert_sore = 0;
	PCCERT_CONTEXT p_cntr_sig_cert = 0;
    HCRYPTPROV prov = NULL;
	HCRYPTMSG h_msg = NULL;
	BYTE * pb_indata = NULL; // ������ �� ����� � ��������� �� ������������ ����������
	DWORD cb_indata = 0;
    BYTE * pb_decoded = NULL; // ��������������� ������ �� ����� � ��������� �� ������������ ����������
	DWORD cb_decoded = 0;
    BYTE * pb_encoded = NULL; // ������ � �������� ������ � ����������
	DWORD cb_encoded = 0;
	CMSG_SIGNER_ENCODE_INFO countersigner_info;
	CMSG_SIGNER_ENCODE_INFO cntr_sign_array[1] = {0};
	SFile file;

	THROW_SL(file.Open(pSignFileName, SFile::mRead | SFile::mBinary));
	int64 file_size = 0;
	file.CalcSize(&file_size);
	cb_indata = (DWORD)file_size;
	THROW_MEM(pb_indata = new BYTE[cb_indata]);
	memzero(pb_indata, cb_indata);
	file.ReadV(pb_indata, cb_indata);
	file.Close();

	// ������� ���������� ����������
	THROW(GetCert(cert_sore, p_cntr_sig_cert, pCountersignerName));

	// ������� ��������� ��� ��������������
	THROW_PP(h_msg = CryptMsgOpenToDecode(MY_ENCODING_TYPE, 0, 0, NULL, NULL, NULL), PPERR_EDS_MSGOPENFAILED);

    // �������� ��������� pb_indata ��� ������������
	THROW_PP(CryptMsgUpdate(h_msg, pb_indata, cb_indata, TRUE), PPERR_EDS_MSGUPDATEFAILED);

	// ������� ������ ���������������� ���������
	THROW_PP(CryptMsgGetParam(h_msg, CMSG_CONTENT_PARAM, 0, NULL, &cb_decoded), PPERR_EDS_GETSIGNINFOFAILED);

	// ������� ������
	THROW_MEM(pb_decoded = new BYTE[cb_decoded]);
	memzero(pb_decoded, cb_decoded);

	// �������� � pb_decoded ��������������� ���������
    THROW_PP(CryptMsgGetParam(h_msg, CMSG_CONTENT_PARAM, 0, pb_decoded, &cb_decoded), PPERR_EDS_GETSIGNINFOFAILED);

	// ��������� �������
	THROW(VerifySign(pFileName, pSignFileName, signerNumber));

	// ������ ����� ������� ���������� �������
	// �������� ����������������
	DWORD dw_key_spec;
	THROW(GetCryptoProv(p_cntr_sig_cert, prov, dw_key_spec));

	// �������������� ��������� ��� ���������� �������
    memzero(&countersigner_info, sizeof(CMSG_SIGNER_ENCODE_INFO));
    countersigner_info.cbSize = sizeof(CMSG_SIGNER_ENCODE_INFO);
    countersigner_info.pCertInfo = p_cntr_sig_cert->pCertInfo;
    countersigner_info.hCryptProv = prov;
    countersigner_info.dwKeySpec = dw_key_spec;
    countersigner_info.HashAlgorithm.pszObjId = p_cntr_sig_cert->pCertInfo->SignatureAlgorithm.pszObjId/*szOID_RSA_MD5*//*MY_HASH_ALG*/;

    cntr_sign_array[0] = countersigner_info;

	// �������� �������
	THROW_PP(CryptMsgCountersign(h_msg, signerNumber - 1, 1, cntr_sign_array), PPERR_EDS_CREATESIGNATUREFAILED);

	// �������� ������ �� �����, � ���������� ��������, ���������.
    // �������� ����� ����������� ���������
	THROW_PP(CryptMsgGetParam(h_msg, CMSG_ENCODED_MESSAGE, 0, NULL, &cb_encoded), PPERR_EDS_CREATESIGNATUREFAILED);

	// �������� ������
	THROW_MEM(pb_encoded = new BYTE[cb_encoded]);
	memzero(pb_encoded, cb_encoded);

	// �������� ����������� ���������� �������� ���������
	THROW_PP(CryptMsgGetParam(h_msg, CMSG_ENCODED_MESSAGE, 0, pb_encoded, &cb_encoded), PPERR_EDS_CREATESIGNATUREFAILED);

	THROW_SL(file.Open(pSignFileName, SFile::mWrite | SFile::mBinary));
	file.Write(pb_encoded, cb_encoded);
	file.Close();

	CATCHZOK;
	if(h_msg)
	    CryptMsgClose(h_msg);
	if(prov) {
        CryptReleaseContext(prov, 0);
        prov = NULL;
    }
	if(pb_decoded)
		ZDELETE(pb_decoded);
	if(pb_encoded)
		ZDELETE(pb_encoded);
	if(pb_indata)
		ZDELETE(pb_indata);
	if(p_cntr_sig_cert) {
		CertFreeCertificateContext(p_cntr_sig_cert);
		p_cntr_sig_cert = 0;
	}
	if(cert_sore)
		CertCloseStore(cert_sore, 0);
	return ok;
}

int PPEds::DeleteSign(const char * pSignFileName, int signNumber)
{
	int ok = 1, cert_index = 0;
	HCERTSTORE cert_sore = 0;
	PCCERT_CONTEXT p_signer_cert = 0;
    HCRYPTPROV prov = NULL;
    HCRYPTMSG h_msg = NULL;
	BYTE * pb_indata = NULL; // ������ �� ����� � ��������� �� �������� �������
	DWORD cb_indata = 0;
    BYTE * pb_unsigned_message_blob = NULL; // ����� ������ � ��������, ��������� ������� �������
	DWORD cb_unsigned_message_blob = 0;

	SFile file;
	THROW_SL(file.Open(pSignFileName, SFile::mRead | SFile::mBinary));
	int64 file_size = 0;
	file.CalcSize(&file_size);
	cb_indata = (DWORD)file_size;
	THROW_MEM(pb_indata = new BYTE[cb_indata]);
	memzero(pb_indata, cb_indata);
	file.ReadV(pb_indata, cb_indata);
	file.Close();

    DWORD dw_key_spec = 0;

	// ��������, � ���� �� ������� ������
	int sign_count = 0;
	THROW(GetSignsCount(pSignFileName, sign_count));
	if(sign_count != 0) { // ���� ���� ���� �� ���� �������
		SString signer_name;
		THROW(GetSignerNameByNumber(pSignFileName, signNumber, signer_name));
		// � �������� ��� �������� ������� �������� ������ �� ����
		// �������� ����������
		//THROW(GetCert(cert_sore, p_signer_cert, signer_name));
		//// �������� ����������������
		//THROW(GetCryptoProv(p_signer_cert, prov, dw_key_spec));
		//// �������� ������� ������������
		//THROW(CheckCertChain(p_signer_cert));

		// ������� ������ ����������� ���������� ����������
		THROW(GetCertIndexBySignerName(pSignFileName, signer_name, cert_index));

		// ������� ��������� ��� ��������������
		THROW_PP(h_msg = CryptMsgOpenToDecode(MY_ENCODING_TYPE, 0, 0, NULL, NULL, NULL), PPERR_EDS_MSGOPENFAILED);

		// ������������� pb_indata � ����� ��������� � h_msg
		THROW_PP(CryptMsgUpdate(h_msg, pb_indata, cb_indata, TRUE), PPERR_EDS_MSGUPDATEFAILED);

		// ������� ���� � ���������� �� ���������
		int true_index = signNumber - 1; // ��� ��� ��� ������� ���������� �������� �������, ������� � 1, �
								// � ��������� �������� ������ ���������� � 0
		THROW_PP(CryptMsgControl(h_msg, 0, CMSG_CTRL_DEL_SIGNER, &true_index), PPERR_EDS_DELSIGNFAILED);

		// ������� ���������� ���������� �� ���������
		THROW_PP(CryptMsgControl(h_msg, 0, CMSG_CTRL_DEL_CERT, &cert_index), PPERR_EDS_DELCERTINFOFAILED);

		// �������� ������ ��������� ��� �������
		THROW_PP(CryptMsgGetParam(h_msg, CMSG_ENCODED_MESSAGE, 0, NULL, &cb_unsigned_message_blob), PPERR_EDS_DELSIGNFAILED);

		// �������� ������ ��� ��������� ��� �������
		THROW_MEM(pb_unsigned_message_blob = new BYTE[cb_unsigned_message_blob]);
		memzero(pb_unsigned_message_blob, cb_unsigned_message_blob);

		// �������� ��������� ��� �������
		THROW_PP(CryptMsgGetParam(h_msg, CMSG_ENCODED_MESSAGE, 0, pb_unsigned_message_blob, &cb_unsigned_message_blob), PPERR_EDS_DELSIGNFAILED);

		THROW_SL(file.Open(pSignFileName, SFile::mWrite | SFile::mBinary));
		file.Write(pb_unsigned_message_blob, cb_unsigned_message_blob);
		file.Close();
	}

	CATCHZOK;
	if(h_msg)
        CryptMsgClose(h_msg);
	if(prov) {
        CryptReleaseContext(prov, 0);
        prov = NULL;
    }
	if(pb_unsigned_message_blob)
		ZDELETE(pb_unsigned_message_blob);
	if(pb_indata)
		ZDELETE(pb_indata);
	if(p_signer_cert) {
		CertFreeCertificateContext(p_signer_cert);
		p_signer_cert = 0;
	}
	if(cert_sore)
		CertCloseStore(cert_sore, 0);
	return ok;
}

int PPEds::DeleteCountersign(char * pSignFileName, const int signerNumber)
{
	int ok = 1;
    HCRYPTMSG h_msg = NULL;
	BYTE * pb_indata = NULL; // ������ ����� � ��������� �� �������� ����������
    BYTE * pb_unsigned_message_blob = NULL;
	DWORD cb_indata = 0;
	PCMSG_SIGNER_INFO pb_signer_info = 0; // ����� ������ � ��������, ���������� �������
	DWORD cb_unsigned_message_blob = 0;
	DWORD cb_signer_info = 0;
	CMSG_CTRL_DEL_SIGNER_UNAUTH_ATTR_PARA unauth_para;

	SFile file;
	THROW_SL(file.Open(pSignFileName, SFile::mRead | SFile::mBinary));
	int64 file_size = 0;
	file.CalcSize(&file_size);
	cb_indata = (DWORD)file_size;
	THROW_MEM(pb_indata = new BYTE[cb_indata]);
	memzero(pb_indata, cb_indata);
	file.ReadV(pb_indata, cb_indata);
	file.Close();

    DWORD dw_key_spec = 0;

	// ������� ��������� ��� ��������������
	THROW_PP(h_msg = CryptMsgOpenToDecode(MY_ENCODING_TYPE, 0, 0, NULL, NULL, NULL), PPERR_EDS_MSGOPENFAILED);

	// ������� ������ � �������� � h_msg
	THROW_PP(CryptMsgUpdate(h_msg, pb_indata, cb_indata, TRUE), PPERR_EDS_MSGUPDATEFAILED);

	// ������� ���� � �������
	// ������� ������ ������ � ��������
	THROW_PP(CryptMsgGetParam(h_msg, CMSG_SIGNER_INFO_PARAM, signerNumber - 1, NULL, &cb_signer_info), PPERR_EDS_GETSIGNINFOFAILED);

	// ������� ������
	THROW_MEM(pb_signer_info = (PCMSG_SIGNER_INFO)LocalAlloc(LPTR, cb_signer_info));
	// ������� ���� � �������
	THROW_PP(CryptMsgGetParam(h_msg, CMSG_SIGNER_INFO_PARAM, signerNumber - 1, pb_signer_info, &cb_signer_info), PPERR_EDS_GETSIGNINFOFAILED);
	memzero(&unauth_para, sizeof(CMSG_CTRL_DEL_SIGNER_UNAUTH_ATTR_PARA));
	unauth_para.cbSize = sizeof(CMSG_CTRL_DEL_SIGNER_UNAUTH_ATTR_PARA);
	if(pb_signer_info->UnauthAttrs.cAttr) {
		for(uint j = 0; j < pb_signer_info->UnauthAttrs.cAttr; j++) {
			unauth_para.dwSignerIndex = signerNumber - 1;
			unauth_para.dwUnauthAttrIndex = j;
			// ����� ���������� ����� ��������� ����� �������, ��� ��� ��������, ��� ��������
			// ���-���� �������� ��������
			DWORD cb_size = 0;
			if(CryptDecodeObject(MY_ENCODING_TYPE, PKCS7_SIGNER_INFO,
				pb_signer_info->UnauthAttrs.rgAttr[j].rgValue->pbData,
				pb_signer_info->UnauthAttrs.rgAttr[j].rgValue->cbData, 0, NULL, &cb_size)) {
				// ������� ���� � ���������� �� ���������
				THROW_PP(CryptMsgControl(h_msg, 0, CMSG_CTRL_DEL_SIGNER_UNAUTH_ATTR, &unauth_para), PPERR_EDS_DELSIGNFAILED);
			}
		}
		// �������� ������ ��������� ��� �������
		THROW_PP(CryptMsgGetParam(h_msg, CMSG_ENCODED_MESSAGE, 0, NULL, &cb_unsigned_message_blob), PPERR_EDS_DELSIGNFAILED);
		// �������� ������ ��� ��������� ��� �������
		THROW_MEM(pb_unsigned_message_blob = new BYTE[cb_unsigned_message_blob]);
		// �������� ��������� ��� �������
		THROW_PP(CryptMsgGetParam(h_msg, CMSG_ENCODED_MESSAGE, 0, pb_unsigned_message_blob, &cb_unsigned_message_blob), PPERR_EDS_DELSIGNFAILED);
		THROW_SL(file.Open(pSignFileName, SFile::mWrite | SFile::mBinary));
		file.Write(pb_unsigned_message_blob, cb_unsigned_message_blob);
		file.Close();
	}
	else
		ok = -1;

	CATCHZOK;
	if(h_msg)
        CryptMsgClose(h_msg);
	if(pb_unsigned_message_blob)
		ZDELETE(pb_unsigned_message_blob);
	if(pb_indata)
		ZDELETE(pb_indata);
	if(pb_signer_info)
		LocalFree(pb_signer_info);
	return ok;
}

int PPEds::GetSignsCount(const char * pSignFileName, int & rCount)
{
	int ok = 1;
	BYTE * pb_indata = NULL;
	DWORD cb_indata = 0;
	SFile file;
	if(!file.Open(pSignFileName, SFile::mRead | SFile::mBinary)) {
		rCount = 0; // ��� ����� � ��������� - ��� ��������
	}
	else {
		int64 file_size = 0;
		file.CalcSize(&file_size);
		cb_indata = (DWORD)file_size;
		THROW_MEM(pb_indata = new BYTE[cb_indata]);
		memzero(pb_indata, cb_indata);
		file.ReadV(pb_indata, cb_indata);
		file.Close();
		rCount = CryptGetMessageSignerCount(MY_ENCODING_TYPE, pb_indata, cb_indata);
		THROW_PP(rCount != -1, PPERR_EDS_GETSIGNCOUNTFAILED)
	}
	CATCHZOK;
	if(pb_indata)
		ZDELETE(pb_indata);
	return ok;
}

int PPEds::SignData(const char * pSignerName, const char * pFileName, SString & rSignFileName)
{
	int ok = 1, r = 1;
	int signs_count = 0;

	if(rSignFileName.NotEmpty())
		THROW(GetSignsCount(rSignFileName, signs_count) != -1); // ������
	if(!rSignFileName.NotEmpty() || (signs_count == 0))
		THROW(FirstSignData(pSignerName, pFileName, rSignFileName))
	else if(signs_count > 0)
		THROW(CoSignData(pSignerName, pFileName, rSignFileName));
	CATCHZOK;
	return ok;
}

int PPEds::GetSignerNameByNumber(const char * pSignFileName, int signNumber, SString & rSignerName)
{
	int ok = 1;
	int64 file_size = 0;
	HCRYPTMSG h_msg = 0;
	PCERT_INFO pb_signer_cert_info = NULL;
	DWORD cb_signer_cert_info = 0;
	HCERTSTORE h_cert_store = 0;
	PCCERT_CONTEXT p_cert = NULL;
	BYTE * pb_indata = NULL;
	DWORD cb_indata = 0;
	cb_indata = 0;
	SFile file;

	THROW_SL(file.Open(pSignFileName, SFile::mRead | SFile::mBinary));
	file.CalcSize(&file_size);
	cb_indata = (DWORD)file_size;
	THROW_MEM(pb_indata = new BYTE[cb_indata]);
	memzero(pb_indata, cb_indata);
	file.ReadV(pb_indata, cb_indata);
	file.Close();

	// ������� ��������� ��� ��������������
    THROW_PP(h_msg = CryptMsgOpenToDecode(MY_ENCODING_TYPE, 0, 0, NULL, NULL, NULL), PPERR_EDS_MSGOPENFAILED);

    // ������������� pb_indata � ����� ��������� � h_msg
    THROW_PP(CryptMsgUpdate(h_msg, pb_indata, cb_indata, TRUE), PPERR_EDS_MSGUPDATEFAILED);

	// ������� ������ ������������� ���� � �����������
	THROW_PP(CryptMsgGetParam(h_msg, CMSG_SIGNER_CERT_INFO_PARAM, signNumber - 1, NULL, &cb_signer_cert_info), PPERR_EDS_GETCERTINFOFAILED);
//		if(GetLastError() == CRYPT_E_INVALID_INDEX) {
//			//cout << "No signers" << endl;
//			PPOutputMessage("No signer with this number", mfYes | mfLargeBox);
////			THROW(0);
//				ok = -1;
//		}
//		else {
//			ShowError("GetSignerNameByNumber: Getting size of the certificate data failed ");
//			THROW(0);
//		}
//	}
//	if(ok) {
		THROW_MEM(pb_signer_cert_info = (PCERT_INFO) SAlloc::M(cb_signer_cert_info));
		memzero(pb_signer_cert_info, cb_signer_cert_info);
		// �������� ���� � �����������
		THROW_PP(CryptMsgGetParam(h_msg, CMSG_SIGNER_CERT_INFO_PARAM, signNumber - 1, pb_signer_cert_info, &cb_signer_cert_info), PPERR_EDS_GETCERTINFOFAILED);
		// ������� ��������� ������������ � ������ � ������� CERT_STORE_PROV_MSG,
		// ������� �������������� ��� � ������������� �� ���������.
		THROW_PP(h_cert_store = CertOpenStore(CERT_STORE_PROV_MSG, MY_ENCODING_TYPE, NULL, 0, h_msg), PPERR_EDS_OPENCERTSTORE);
		// ������ ���������� ����������
		THROW_PP(p_cert = CertGetSubjectCertificateFromStore(h_cert_store, MY_ENCODING_TYPE, pb_signer_cert_info), PPERR_EDS_GETCERT);
		// ������� ��� ����������
		char signer_name[256];
		THROW_PP(CertGetNameString(p_cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, NULL, signer_name, MAX_NAME_LEN) > 1, PPERR_EDS_GETSIGNERNAMEFAILED); // @unicodeproblem
		rSignerName.Cat(signer_name);
//	}

	CATCHZOK;

	if(pb_signer_cert_info)
        ZFREE(pb_signer_cert_info);
	if(pb_indata)
		ZDELETE(pb_indata);
	if(h_msg)
        CryptMsgClose(h_msg);
	if(p_cert) {
        CertFreeCertificateContext(p_cert);
		p_cert = 0;
	}
    if(h_cert_store)
        CertCloseStore(h_cert_store, CERT_CLOSE_STORE_FORCE_FLAG);
	return ok;
}

int PPEds::GetCertIndexBySignerName(const char * pSignFileName, const char * pSignerName, int & rCertIndex)
{
	int ok = 1, signers_count = 0, index = 0, exit = 0;
	int64 file_size = 0;
	HCRYPTMSG h_msg = 0;
	PCCERT_CONTEXT p_cert = NULL;
	BYTE * pb_indata = NULL;
	DWORD cb_indata = 0;
	BYTE * pb_cert = 0;
	DWORD cb_cert;
	SFile file;

	THROW_SL(file.Open(pSignFileName, SFile::mRead | SFile::mBinary));
	file.CalcSize(&file_size);
	cb_indata = (DWORD)file_size;
	THROW_MEM(pb_indata = new BYTE[cb_indata]);
	memzero(pb_indata, cb_indata);
	file.ReadV(pb_indata, cb_indata);
	file.Close();

	// ������� ��������� ��� ��������������
    THROW_PP(h_msg = CryptMsgOpenToDecode(MY_ENCODING_TYPE, 0, 0, NULL, NULL, NULL), PPERR_EDS_MSGOPENFAILED);

    // ��������� ������ � �������� � h_msg
    THROW_PP(CryptMsgUpdate(h_msg, pb_indata, cb_indata, TRUE), PPERR_EDS_MSGUPDATEFAILED);

	THROW(GetSignsCount(pSignFileName, signers_count));
	while(!exit && (index != signers_count)) {
		// ������� ������ ������������� ���� � �����������
		THROW_PP(CryptMsgGetParam(h_msg, CMSG_CERT_PARAM, index, NULL, &cb_cert), PPERR_EDS_GETCERTINFOFAILED);
		// �������� ������
		THROW_MEM(pb_cert = new BYTE[cb_cert]);
		memzero(pb_cert, cb_cert);

		// �������� ���� � �����������
		THROW_PP(CryptMsgGetParam(h_msg, CMSG_CERT_PARAM, index, pb_cert, &cb_cert), PPERR_EDS_GETCERTINFOFAILED);

		// ������ ���������� ����������
		THROW_PP(p_cert = CertCreateCertificateContext(MY_ENCODING_TYPE, pb_cert, cb_cert), PPERR_EDS_GETCERT);
		// ������� ��� ����������
		char signer_name[256];
		THROW_PP(CertGetNameString(p_cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, NULL,
			signer_name, MAX_NAME_LEN) > 1, PPERR_EDS_GETSIGNERNAMEFAILED); // @unicodeproblem
		if(strcmp(signer_name, pSignerName) == 0)
			exit = 1;
		else
			index++;
	}
	rCertIndex = index;

	CATCHZOK;

	if(pb_indata)
		ZDELETE(pb_indata);
	if(pb_cert)
		ZDELETE(pb_cert);
	if(h_msg)
        CryptMsgClose(h_msg);
	if(p_cert) {
        CertFreeCertificateContext(p_cert);
		p_cert = 0;
	}
	return ok;
}

int PPEds::CashOn(PCCERT_CONTEXT & cert)
{
	int ok = 1;
	DWORD c_data;
	CRYPT_KEY_PROV_INFO * p_crypt_key_prov_info  = NULL;

    THROW(CertGetCertificateContextProperty(cert, CERT_KEY_PROV_INFO_PROP_ID, NULL, &c_data));
    THROW_MEM(p_crypt_key_prov_info = (CRYPT_KEY_PROV_INFO *)SAlloc::M(c_data));
    THROW(CertGetCertificateContextProperty(cert, CERT_KEY_PROV_INFO_PROP_ID, p_crypt_key_prov_info, &c_data));
    // ��������� ���� ����������� ����������
    p_crypt_key_prov_info->dwFlags = CERT_SET_KEY_CONTEXT_PROP_ID;
    // ��������� �������� � ��������� �����������
    THROW(CertSetCertificateContextProperty(cert, CERT_KEY_PROV_INFO_PROP_ID, 0, p_crypt_key_prov_info));

	CATCHZOK;
	if(p_crypt_key_prov_info)
		ZFREE(p_crypt_key_prov_info);
	return ok;
}

int PPEds::CashOff(PCCERT_CONTEXT & cert)
{
	int ok = 1;
	DWORD c_data;
	CRYPT_KEY_PROV_INFO * p_crypt_key_prov_info  = NULL;

	THROW(CertGetCertificateContextProperty(cert, CERT_KEY_PROV_INFO_PROP_ID, NULL, &c_data));
    if(p_crypt_key_prov_info)
        SAlloc::F(p_crypt_key_prov_info);
    THROW_MEM(p_crypt_key_prov_info = (CRYPT_KEY_PROV_INFO *)SAlloc::M(c_data));
    THROW(CertGetCertificateContextProperty(cert, CERT_KEY_PROV_INFO_PROP_ID, p_crypt_key_prov_info, &c_data));
    /* ������� ���� ����������� ����������*/
    p_crypt_key_prov_info->dwFlags = 0;
    /* ��������� �������� � ��������� �����������*/
	THROW(CertSetCertificateContextProperty(cert, CERT_KEY_PROV_INFO_PROP_ID, 0, p_crypt_key_prov_info));

	CATCHZOK;
	if(p_crypt_key_prov_info)
		ZFREE(p_crypt_key_prov_info);
	return ok;
}

int PPEds::GetCountersignerNameBySignerNumber(const char * pSignFileName, int signerNumber, SString & rCounterSignerName)
{
	int ok = 1, sign_count = 0;
	int64 file_size = 0;
	HCERTSTORE store = 0;
	PCCERT_CONTEXT p_cert = 0;
	CERT_INFO cert_info;
	HCRYPTMSG h_msg = 0;
	PCMSG_SIGNER_INFO pb_countersigner_info = NULL; // ���� � ����������
	DWORD cb_countersigner_info = 0;
	BYTE * pb_sign_data = NULL; // ������ �� ����� � ���������
	DWORD cb_sign_data = 0;
	BYTE * pb_signer_info = NULL; // ���� � ����������, ��� ������� ��������
	DWORD cb_signer_info = 0;
    PCRYPT_ATTRIBUTES pb_attr_info = NULL; // ���� � ���������� �������
	DWORD cb_attr_info = 0;
	SFile file;

	// ������� ������ �� ����� � ��������
	THROW_SL(file.Open(pSignFileName, SFile::mRead | SFile::mBinary));
	file.CalcSize(&file_size);
	cb_sign_data = (DWORD)file_size;
	THROW_MEM(pb_sign_data = new BYTE[cb_sign_data]);
	memzero(pb_sign_data, cb_sign_data);
	file.ReadV(pb_sign_data, cb_sign_data);
	file.Close();

	THROW_PP(h_msg = CryptMsgOpenToDecode(MY_ENCODING_TYPE, 0, 0, 0, NULL, NULL), PPERR_EDS_MSGOPENFAILED);
	// ������� � h_msg ���� � ��������
    THROW_PP(CryptMsgUpdate(h_msg, pb_sign_data, cb_sign_data, TRUE), PPERR_EDS_MSGUPDATEFAILED);

	// ������� ���� � �������
	// ������� ������ ������ � �������
	THROW_PP(CryptMsgGetParam(h_msg, CMSG_ENCODED_SIGNER, signerNumber - 1, NULL, &cb_signer_info), PPERR_EDS_GETSIGNINFOFAILED);

	// ������� ������
	THROW_MEM(pb_signer_info = new BYTE[cb_signer_info]);
	memzero(pb_signer_info, cb_signer_info);

	// ������� ���� � �������
	THROW_PP(CryptMsgGetParam(h_msg, CMSG_ENCODED_SIGNER, signerNumber - 1, pb_signer_info, &cb_signer_info), PPERR_EDS_GETSIGNINFOFAILED);

	// �������� ���� � ���������� �������
	// �������� �������� ���������� ������� �� ��� ������������ ����������
	// �������� ������ ������ � ���������� ��������
	THROW_PP(CryptMsgGetParam(h_msg, CMSG_SIGNER_UNAUTH_ATTR_PARAM, signerNumber - 1, NULL, &cb_attr_info), PPERR_EDS_GETSIGNINFOFAILED);

	// ������� ������
	THROW_MEM(pb_attr_info = (CRYPT_ATTRIBUTES*)SAlloc::M(cb_attr_info));
	memzero(pb_attr_info, cb_attr_info);

	// �������� ���� � ���������� �������
	THROW_PP(CryptMsgGetParam(h_msg, CMSG_SIGNER_UNAUTH_ATTR_PARAM, signerNumber - 1,
		pb_attr_info, &cb_attr_info), PPERR_EDS_GETSIGNINFOFAILED);

	for(size_t j = 0; j < pb_attr_info->cAttr; j ++) {
		// ������� ���������� ����������
		// ������� �� ������ � ���������� ������� ���� � ����������
		// ������� ������ ������
		if(CryptDecodeObject(MY_ENCODING_TYPE, PKCS7_SIGNER_INFO,
			pb_attr_info->rgAttr[j].rgValue->pbData,
			pb_attr_info->rgAttr[j].rgValue->cbData, 0, NULL, &cb_countersigner_info)) {
			// ������� ������
			THROW_MEM(pb_countersigner_info = (PCMSG_SIGNER_INFO)SAlloc::M(cb_countersigner_info));
			memzero(pb_countersigner_info, cb_countersigner_info);
			THROW_PP(CryptDecodeObject(MY_ENCODING_TYPE, PKCS7_SIGNER_INFO,
				pb_attr_info->rgAttr[j].rgValue->pbData,
				pb_attr_info->rgAttr[j].rgValue->cbData, 0, pb_countersigner_info, &cb_countersigner_info), PPERR_EDS_MSGOPENFAILED);

			cert_info.Issuer = pb_countersigner_info->Issuer;
			cert_info.SerialNumber = pb_countersigner_info->SerialNumber;
			// ������� ��������� ������������
			THROW_PP(store = CertOpenSystemStore(0, CERTIFICATE_STORE_NAME), PPERR_EDS_OPENCERTSTORE); // @unicodeproblem
			// ������� ���������� ����������
			THROW_PP((p_cert = CertGetSubjectCertificateFromStore(store, MY_ENCODING_TYPE, &cert_info)), PPERR_EDS_GETCERT);
			// ������� ��� ����������
			char countersigner_name[256];
			THROW_PP(CertGetNameString(p_cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, NULL,
					countersigner_name, MAX_NAME_LEN) > 1, PPERR_EDS_GETSIGNERNAMEFAILED); // @unicodeproblem
			rCounterSignerName.Cat(countersigner_name);
		}
	}

	CATCHZOK;
	if(pb_sign_data)
		ZDELETE(pb_sign_data);
	if(pb_signer_info)
		ZDELETE(pb_signer_info);
	if(pb_countersigner_info)
		ZFREE(pb_countersigner_info);
	if(h_msg)
        CryptMsgClose(h_msg);
	if(p_cert) {
        CertFreeCertificateContext(p_cert);
		p_cert = 0;
	}
    if(store)
        CertCloseStore(store, CERT_CLOSE_STORE_FORCE_FLAG);
	return ok;
}

int PPEds::GetSignerNamesInStore(StrAssocArray & rStrArray)
{
	int    ok = 1;
	long   i = 1;
	HCERTSTORE store = 0;
	PCCERT_CONTEXT p_cert = 0;
	char   signer_name[MAX_NAME_LEN];
	SString name;
	THROW_PP(store = CertOpenSystemStore(0, CERTIFICATE_STORE_NAME), PPERR_EDS_OPENCERTSTORE); // @unicodeproblem
	while(p_cert = CertEnumCertificatesInStore(store, p_cert)) {
		THROW_PP(CertGetNameString(p_cert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, NULL, signer_name, MAX_NAME_LEN), PPERR_EDS_GETSIGNERNAMEFAILED); // @unicodeproblem
		name.Z().Cat(signer_name);
		rStrArray.Add(i - 1, name, 1);
		i++;
	}
	if(i == 0) {
		SString msg;
		msg.Z().Cat("No certificates was found in store ").Cat(CERTIFICATE_STORE_NAME);
		PPOutputMessage(msg, mfYes | mfLargeBox);
	}
	CATCHZOK;
	if(p_cert) {
        CertFreeCertificateContext(p_cert);
		p_cert = 0;
	}
    if(store)
        CertCloseStore(store, CERT_CLOSE_STORE_FORCE_FLAG);
	return ok;
}

int PPEds::VerifySign(const char * pFileName, const char * pSignFileName, int signerNumber)
{
	int ok = 1;
	int64 file_size = 0;
	HCRYPTPROV prov = 0;
	PCCERT_CONTEXT p_cert = 0;
	HCERTSTORE store = 0;
	SString  signer_name;
	SFile file;
	BYTE * pb_indata = NULL; // ������ �� ���������
	DWORD cb_indata = 0;
	BYTE * pb_sign_data = NULL; // ������ �� ����� � ���������
	DWORD cb_sign_data = 0;
	CRYPT_VERIFY_MESSAGE_PARA verify_params;

	// ������� ������ �� ���������
	THROW_SL(file.Open(pFileName, SFile::mRead | SFile::mBinary));
	file.CalcSize(&file_size);
	cb_indata = (DWORD)file_size;
	THROW_MEM(pb_indata = new BYTE[cb_indata]);
	memzero(pb_indata, cb_indata);
	file.ReadV(pb_indata, cb_indata);
	file.Close();

	// ������� ������ �� ����� � ��������
	THROW_SL(file.Open(pSignFileName, SFile::mRead | SFile::mBinary));
	file_size = 0;
	file.CalcSize(&file_size);
	cb_sign_data = (DWORD)file_size;
	THROW_MEM(pb_sign_data = new BYTE[cb_sign_data]);
	memzero(pb_sign_data, cb_sign_data);
	file.ReadV(pb_sign_data, cb_sign_data);
	file.Close();

	/*THROW(GetSignerNameByNumber(pFileName, signerNumber, signer_name));
	THROW(GetCert(store, p_cert, signer_name));
	DWORD key_spec = 0;
	THROW(GetCryptoProv(p_cert, prov, key_spec));*/

    verify_params.cbSize = sizeof(CRYPT_VERIFY_MESSAGE_PARA);
    verify_params.dwMsgAndCertEncodingType = MY_ENCODING_TYPE;
    verify_params.hCryptProv = NULL/*prov*/;
    verify_params.pfnGetSignerCertificate = NULL; // ����� ������ � ���������
    verify_params.pvGetArg = NULL;

	// ��������� �������
	const BYTE * indata_arr[] = {pb_indata};
	DWORD indata_size_arr[1];
	indata_size_arr[0] = cb_indata;
	if(!CryptVerifyDetachedMessageSignature(&verify_params, signerNumber - 1, pb_sign_data,
		cb_sign_data, 1, indata_arr, indata_size_arr, /*NULL*/&p_cert)) {
		THROW_PP((GetLastError() == CRYPT_E_HASH_VALUE) || (GetLastError() == NTE_BAD_SIGNATURE), PPERR_EDS_SIGNVERIFY);
		//ShowError("VerifySign: Hash data is not correct. Maybe, document was modified.");
		ok = -1;
	}

	CATCHZOK;
	if(pb_indata)
		ZDELETE(pb_indata);
	if(pb_sign_data)
		ZDELETE(pb_sign_data);
	if(p_cert) {
		CertFreeCertificateContext(p_cert);
		p_cert = 0;
	}
	if(prov) {
        CryptReleaseContext(prov, 0);
        prov = NULL;
    }
	if(store)
        CertCloseStore(store, CERT_CLOSE_STORE_FORCE_FLAG);
	return ok;
}

int PPEds::VerifyCountersign(const char * pSignFileName, const int signerNumber)
{
	int ok = 1, sign_count = 0;
	int64 file_size = 0;
	HCRYPTMSG h_msg = 0;
	HCERTSTORE store = 0;
	PCCERT_CONTEXT p_cert = 0;
	CERT_INFO cert_info;
	PCMSG_SIGNER_INFO pb_countersigner_info = NULL; // ���� � ����������
	DWORD cb_countersigner_info = 0;
	BYTE * pb_sign_data = NULL; // ������ �� ����� � ���������
	DWORD cb_sign_data = 0;
	BYTE * pb_signer_info = NULL; // ���� � ����������, ��� ������� ��������
	DWORD cb_signer_info = 0;
    PCRYPT_ATTRIBUTES pb_countersign_info = NULL; // ���� � ���������� �������
	DWORD cb_countersign_info = 0;
	SFile file;

	// ������� ������ �� ����� � ��������
	THROW_SL(file.Open(pSignFileName, SFile::mRead | SFile::mBinary));
	file.CalcSize(&file_size);
	cb_sign_data = (DWORD)file_size;
	THROW_MEM(pb_sign_data = new BYTE[cb_sign_data]);
	memzero(pb_sign_data, cb_sign_data);
	file.ReadV(pb_sign_data, cb_sign_data);
	file.Close();

	THROW_PP(h_msg = CryptMsgOpenToDecode(MY_ENCODING_TYPE, 0, 0, 0, NULL, NULL), PPERR_EDS_MSGOPENFAILED);

    // ������� � h_msg ���� � ��������
    THROW_PP(CryptMsgUpdate(h_msg, pb_sign_data, cb_sign_data, TRUE), PPERR_EDS_MSGUPDATEFAILED);

    // ������� ���� � �������
	// ������� ������ ������ � �������
	THROW_PP(CryptMsgGetParam(h_msg, CMSG_ENCODED_SIGNER, signerNumber - 1, NULL, &cb_signer_info), PPERR_EDS_GETSIGNINFOFAILED);

	// ������� ������
	THROW_MEM(pb_signer_info = new BYTE[cb_signer_info]);
	memzero(pb_signer_info, cb_signer_info);

	// ������� ���� � �������
	THROW_PP(CryptMsgGetParam(h_msg, CMSG_ENCODED_SIGNER, signerNumber - 1, pb_signer_info, &cb_signer_info), PPERR_EDS_GETSIGNINFOFAILED);

	// �������� ���� � ���������� �������
	// �������� ������ ������ � ���������� �������
	THROW_PP(CryptMsgGetParam(h_msg, CMSG_SIGNER_UNAUTH_ATTR_PARAM, signerNumber - 1, NULL, &cb_countersign_info), PPERR_EDS_GETSIGNINFOFAILED);
	// ������� ������
	THROW_MEM(pb_countersign_info = (CRYPT_ATTRIBUTES*)SAlloc::M(cb_countersign_info));
	memzero(pb_countersign_info, cb_countersign_info);

	// �������� ���� � ���������� �������
	THROW_PP(CryptMsgGetParam(h_msg, CMSG_SIGNER_UNAUTH_ATTR_PARAM, signerNumber - 1,
		pb_countersign_info, &cb_countersign_info), PPERR_EDS_GETSIGNINFOFAILED);
	for(size_t i = 0; i < pb_countersign_info->cAttr; i++) {
		// ������� ���������� ����������
		// ������� �� ������ � ���������� ������� ���� � ����������
		// ������� ������ ������ � ������ ��������, �������� �� �������� ��������
		THROW_PP(CryptDecodeObject(MY_ENCODING_TYPE, PKCS7_SIGNER_INFO,
			pb_countersign_info->rgAttr[i].rgValue->pbData,
			pb_countersign_info->rgAttr[i].rgValue->cbData, 0, NULL, &cb_countersigner_info), PPERR_EDS_MSGOPENFAILED);
		// ������� ������
		THROW_MEM(pb_countersigner_info = (PCMSG_SIGNER_INFO)SAlloc::M(cb_countersigner_info));
		memzero(pb_countersigner_info, cb_countersigner_info);
		THROW_PP(CryptDecodeObject(MY_ENCODING_TYPE, PKCS7_SIGNER_INFO,
			pb_countersign_info->rgAttr->rgValue->pbData,
			pb_countersign_info->rgAttr->rgValue->cbData, 0, pb_countersigner_info, &cb_countersigner_info), PPERR_EDS_MSGOPENFAILED);

		cert_info.Issuer = pb_countersigner_info->Issuer;
		cert_info.SerialNumber = pb_countersigner_info->SerialNumber;
		// ������� ��������� ������������
		THROW_PP((store = CertOpenSystemStore(0, CERTIFICATE_STORE_NAME)), PPERR_EDS_OPENCERTSTORE); // @unicodeproblem
		// ������� ���������� ����������
		THROW_PP((p_cert = CertGetSubjectCertificateFromStore(store, MY_ENCODING_TYPE, &cert_info)), PPERR_EDS_GETCERT);
		// ��������� ���������� �������
		if(!CryptMsgVerifyCountersignatureEncoded(0, MY_ENCODING_TYPE, pb_signer_info,
			cb_signer_info, pb_countersign_info->rgAttr->rgValue->pbData,
			pb_countersign_info->rgAttr->rgValue->cbData, p_cert->pCertInfo)) {
			THROW_PP((GetLastError() == CRYPT_E_HASH_VALUE) || (GetLastError() == NTE_BAD_SIGNATURE), PPERR_EDS_COUNTERSIGNNOTVERIFIED);
			ok = -1;
		}
	}

	CATCHZOK;
	if(pb_sign_data)
		ZDELETE(pb_sign_data);
	if(pb_signer_info)
		ZDELETE(pb_signer_info);
	if(pb_countersign_info)
		ZFREE(pb_countersign_info);
	if(pb_countersigner_info)
		ZFREE(pb_countersigner_info);
	if(h_msg)
        CryptMsgClose(h_msg);
	if(p_cert) {
		CertFreeCertificateContext(p_cert);
		p_cert = 0;
	}
	if(store)
        CertCloseStore(store, CERT_CLOSE_STORE_FORCE_FLAG);
	return ok;
}

int PPEds::GetHash(void * pData, DWORD dataSize, int signNumber, BYTE * pHashedData, DWORD & rSizeHashedData)
{
	int ok = 1;
	HCRYPTMSG h_msg;
	THROW_PP(h_msg = CryptMsgOpenToDecode(MY_ENCODING_TYPE, 0, 0, NULL, NULL, NULL), PPERR_EDS_MSGOPENFAILED);
	THROW_PP(CryptMsgUpdate(h_msg, (BYTE*)pData, dataSize, true), PPERR_EDS_MSGUPDATEFAILED);
	if(!pHashedData)
		THROW_PP(CryptMsgGetParam(h_msg, CMSG_COMPUTED_HASH_PARAM, signNumber - 1, NULL, &rSizeHashedData), PPERR_EDS_GETHASHFAILED)
	else
		THROW_PP(CryptMsgGetParam(h_msg, CMSG_COMPUTED_HASH_PARAM, signNumber - 1, pHashedData, &rSizeHashedData), PPERR_EDS_GETHASHFAILED);

	CATCHZOK;
	if(h_msg)
        CryptMsgClose(h_msg);
	return ok;
}

int PPEds::ObjIdfrDerEncode(const char * strToEncode, SString & rEncodedStr)
{
	// ��������� Der-����������� ��������� ���������������
	// ������ ��� ����� ���������� � ���� �� �������:
	//		new_val = val_1 * 40 + val_2
	// ���������� ����� ���������� � 128. ���� ������ ����� ��������, �� ����� �������� ���
	// ���������. ���� ������ ��� �����, �� ����� ������������� �� ���������� ���������:
	//		������� ��� ����� �������� ��������������� ��������� �����. �� ���� � ��������� �����
	//		������� ��� ����� 0, � ��������� - 1. ��������� ���� ��� �������� ��� �����. �������,
	//		������ ��� �������� � ������� ��� 0 ��� 1, ���� ��������� ��� �������� �������� �����
	//		������ ����� ���� � ���� ������� �� ���� ������� �����.
	//		������: ���������� ����� 113549
	//		�������� ��� - 0000 0001 1011 1011 1000 1101
	//
	//		1) ������� ������� ��� �������� �����, ��� ���� ����, ������� � �����, ��������� ����� �� 1 ���
	//			0000 0011 0111 0111 0000 1101
	//		2) �������� � ������� ������� ��� ������� �����, ����� ������� ���� �����, ������ ������ ������� � �����
	//			0000 0110 1111 0111 0000 1101
	//		3) �������, �������� � ������� ������� ��� �������� �����
	//			1000 0110 1111 0111 0000 1101
	//		4) ������������� �������� ��������� ��������, �� ���� ��� ����� ����� (86 F7 0D)
	int  ok = 1, binval = 0, part_val = 0;
	size_t arr_size = 0;
	size_t enc_arr_size = 0;
	int * vals = 0; // ������ ������� � �������������� ����
	int * encode_vals = 0; // ������ � ��������������� �������
	size_t start = 0, pos = 0, count = 0, i = 0, j = 0;
	SString str, l_str, r_str;
	SString sstr;
	sstr = 0;

	str.Z().Cat(strToEncode);
	while(str.Search(".", start, 1, &pos)) {
		start = pos + 1;
		count++;
	}
	THROW(count);
	arr_size = count + 1;
	vals = new int[arr_size];
	memzero(vals, arr_size);
	i = 0;
	while(str.NotEmpty()) {
		str.Divide('.', l_str, r_str);
		vals[i++] = l_str.ToLong();
		str.Z().Cat(r_str);
	}
	// ���������, ���� �� ����� ������ 127 � ������� ���� ��� ��������
	for(i = 0; i < arr_size; i++) {
		if(vals[i] > 127) {
			char c[32];
			memzero(c, 32);
			itoa(vals[i], c, 2);
			enc_arr_size += sstrlen(c) / 8;
			if((enc_arr_size * 8) < sstrlen(c))
				enc_arr_size++;
		}
		else
			enc_arr_size++;
	}
	THROW(enc_arr_size);
	encode_vals = new int[enc_arr_size];
	memzero(encode_vals, enc_arr_size);
	// ������ �������� ����������
	// ������ ��� ����� �������� ������
	encode_vals[0] = vals[0] * 40 + vals[1];
    // � �������� ����� �������, ��� <= 127, ��� ������
	for(i = 2, j = 1; i < arr_size; i++) {
		part_val = 0;
		if(vals[i] <= 127) {
			encode_vals[j++] = vals[i];
		}
		else {
			binval = vals[i];
			// �������, ������� ���� �������� �����
			char cc[32];
			memzero(cc, sizeof(cc));
			itoa(binval, cc, 2);
			count = sstrlen(cc) / 8;
			if(count * 8 < sstrlen(cc))
				count++;
			long mask = 0x7FFFFFFF; // ������ 4-� ���� ����� ����� �� �����
			// � ������� ����� ������� ��� ������ ���� ����.
			part_val = binval;
			mask = 0x7FFFFF80;
			part_val &= mask;
			part_val <<= 1; // ��������� �������� ���� ����� ������ ����� ������
			part_val &= 0x7FFFFF7F; // �������� ���
			part_val |= 0x7F;
			binval |= 0x7FFFFF80;
            binval &= part_val;
			// � ��������� ������ ������� ��� ������ ���� 1. ����������� ���� �������� � ������� ������
			for(size_t k = 1; k < count; k++) {
				mask = 0x7FFFFFFF << (8 * k);
				mask <<= 7;
				// ����� ������� ����� �������� �����
				part_val = binval;
				part_val &= mask;
				part_val <<= 1; // ��������� �������� ���� ����� ������
				part_val |= (long)(0x80 * powl(256, k)); // ��������� ��� � 1
				part_val |= ~mask;
				binval |= mask;
				binval &= part_val;
				// ��� ��� �������� ���������� ����, ��� ��� �������� �� ��� ����� ���
				// ��������� ��������������
				size_t sub_count = 0;
				memzero(cc, 32);
				itoa(binval, cc, 2);
				sub_count = sstrlen(cc) / 8;
				if(sub_count * 8 < (size_t)sstrlen(cc))
					sub_count++;
				if(sub_count > count)
					count = sub_count;
			}
			// ������ �������� �����
			for(size_t k = 0; k < count; k++) {
				mask = 0x7FFFFFFF;
				mask ^= (0xFF << (8 * (count - k - 1)));
				part_val = (binval & ~mask) >> (8 * (count - k - 1));
				encode_vals[j++] = part_val;
			}
		}
	}
	i = 0;
	rEncodedStr.Z();
	while(i < enc_arr_size) {
		char c = encode_vals[i++];
		rEncodedStr.CatChar(c);
	}
	CATCHZOK;
	ZDELETE(vals);
	ZDELETE(encode_vals);
	return ok;
}

// ��������� ������ �������. ������ �� ���� ������������ capicom
// ��� ������������ ������ ������� ���� ������� ���������������� ������ SignedCode, �������
// ������ ��������� ����������� ������. �� ��������� � ������� ������, ������� ����� ���������:
// "The executable file should be of a type that can be signed with Authenticode technology,
// such as an .exe, .dll, .vbs, or .ocx file"
// ������� ������������� ����������� ����������� cryptoAPI

// ��������� ��� �������� ��������� ������, ���������� �� ������ �� ������� ������ �������
struct StTspResponse {
	StTspResponse() {
		ResponseLen = 0;
		PKIStatus = 0; // ������ �������
		PKIFreeText = ""; // �����-�� ��������
		PKIFailureInfo = ""; // �������� � ������ ������ ���������� �������
		HashAlg = ""; // �������� �����������
		Hash = ""; // ���
		Time.SetZero(); // ����� ������������ ������
		Accuracy = 0; // �������� �������
		Nonce = ""; // ���� �� ������ �����, ������������� �������
	}
	int ResponseLen;
	int PKIStatus;
	SString PKIFreeText;
	SString PKIFailureInfo;
	SString HashAlg; // ����� ������������ ��� ��������, �� ��� �� ������ ������ �����
	SString Hash; // ����� �� ��� ����� ��������
	LDATETIME Time;
	int Accuracy; // �������� ������� ��������� ������
	SString Nonce; // ��� ����������� � ������������� ��� ��������, �� ������ � ������,
					// ���� � ������� ���� �������� ��� ����
};

int PPEds::ParseTSAResponse(const char * pResponse, StTspResponse & rResponseFmtd) {
	int ok = 1;
	char c = 0;
	int block_len = 0;
	int byte_count = 0;
	size_t byte_pos = 0;
	// ��� �������� ��� ����� ������ ������, �������� �����������, ��� ���, ���� nonce,
	// ����� �������� ������.
	// �� ���� ������� ������� ������ ������� ���� �� ����. ������-�� ���� ���������.

	// ������ ������� ������ - ������ �����������.� ��������� ��������� �����,
	// �������� ���������. ��������� ������: ��� - ����� ����� ������ - ������.
	// ������������ �� ������������ ��������� ������. �� ���� ����� ����������, ������������ �
	// ������� ����� ����������. ������� � ���� ��������� ��������� - ����� �� ��� �
	// ����������� ������� � ������� (�������������� ��� ����� � � ������).

	// ���������, ��� ��� ������������������
	THROW((c = pResponse[byte_pos++]) == 48);
	// ������� ����� ����� ������ (����� ���� �� ������� �����)
	c = pResponse[byte_pos++];
	if(c < 0) { // ����� ������� �� ���������� ����. ������ ����� ��� ���� ������,
		// ���� ����������� ����, ���������� �����
		byte_count = 0;
		byte_count = (c & 0x7F); // ������ ��� �����������, ��������� ���� �������� ���������� ���� �����
		c = pResponse[byte_pos++];
		block_len = c;
		for(size_t i = 1; i < (size_t)byte_count; i++) {
			c = pResponse[byte_pos++];
			block_len <<= 8;
			block_len |= c;
		}
		rResponseFmtd.ResponseLen = block_len;
	}
	else
		rResponseFmtd.ResponseLen = c;
	// ������ ������ ���� ������ ������
	THROW((c = pResponse[byte_pos++]) == 48); // ������ ���� ������������������
	block_len = pResponse[byte_pos++]; // ��������� ����� �����
	THROW((c = pResponse[byte_pos++]) == 2); // ��� INTEGER
	THROW((c = pResponse[byte_pos++]) == 1); // ����� 1
	rResponseFmtd.PKIStatus = pResponse[byte_pos++]; // ���������� ��� ������
	if((rResponseFmtd.PKIStatus == 0) || (rResponseFmtd.PKIStatus == 1)) { // ���� ������ ��������, �� ����� �� ��� �����.
								// �������, ����� ����� ���������� ��������� �� ������, ������� ������ ������,
								// �� ���� ��������� ����.
		byte_pos += block_len - 3; // ���������� ��������� ���� � �������� �������� ������ PKIFreeText
									// � PKIFailureInfo
		// ������ ������� ��������� TimeStampToken
		THROW((c = pResponse[byte_pos++]) == 48); // ������������������
		c = pResponse[byte_pos++]; // ����� ��� ����� ����� �� ����� �����, � ���������� ���� ��� �����
		if(c < 0) {
			byte_count = c & 0x7F;
			byte_pos += byte_count;
		}
		// ���������� ContentType
		THROW((c = pResponse[byte_pos++]) == 6); // OBJECT IDENTIFIER
		block_len = pResponse[byte_pos++];
		byte_pos += block_len;
		// ���������� ��������� ���� ��� ����� � ��� -96
		byte_pos++; // ��� (� ���� ��������� -96. ��� �� ����� - �� �������)
		c = pResponse[byte_pos++];
		if(c < 0) {
			byte_count = c & 0x7F;
			byte_pos += byte_count;
		}
		// ���������� ���������� ���� ��� ����� � ��� SignedData ::= SEQUENCE, �� � ��������� ����
		THROW((c = pResponse[byte_pos++]) == 48); // ������������������
		c = pResponse[byte_pos++];
		if(c < 0) {
			byte_count = c & 0x7F;
			byte_pos += byte_count;
		}
		// ���������� ������, �� � ��������� ����
		THROW((c = pResponse[byte_pos++]) == 2); // INTEGER
		byte_pos += 2; // ���� ����� � ���� � ������� ������
		// ���������� DigestAlgorithmIdentifiers �� ����� ���������� �����������, � ��������� ����
		THROW((c = pResponse[byte_pos++]) == 49); // SET OF
		block_len = pResponse[byte_pos++]; // � ������ ����� �� ��������������, ��� ��� ���������
		byte_pos += block_len;
		// ���������� ���������� ���� ��� ����� � ��� EncapsulatedContentInfo ::= SEQUENCE
		THROW((c = pResponse[byte_pos++]) == 48); // ������������������
		c = pResponse[byte_pos++];
		if(c < 0) {
			byte_count = c & 0x7F;
			byte_pos += byte_count;
		}
		// ���������� ����� � ������ � ����� ContentType ::= OBJECT IDENTIFIER, �� � ��������� ����
		THROW((c = pResponse[byte_pos++]) == 6); // OBJECT IDENTIFIER
		block_len = pResponse[byte_pos++];
		byte_pos += block_len;
		// ���������� ����� � ������ � ����� -96
		byte_pos ++; // ��� -96
		c = pResponse[byte_pos++];
		if(c < 0) {
			byte_count = c & 0x7F;
			byte_pos += byte_count;
		}
		// ���������� ����� � ������ � ����� eContent :: OCTET STRING � ��������� ����
		THROW((c = pResponse[byte_pos++]) == 4); // OCTET STRING
		c = pResponse[byte_pos++];
		if(c < 0) {
			byte_count = c & 0x7F;
			byte_pos += byte_count;
		}
		// ���������� ����� � ������ � ����� TSTInfo ::= SEQUENCE � ��������� ����
		THROW((c = pResponse[byte_pos++]) == 48); // ������������������
		c = pResponse[byte_pos++];
		if(c < 0) {
			byte_count = c & 0x7F;
			byte_pos += byte_count;
		}
		// ���������� ������, �� � ��������� ����
		THROW((c = pResponse[byte_pos++]) == 2); // INTEGER
		byte_pos += 2; // ���� ����� � ���� � ������� ������
		// ���������� ��������, �� � ��������� ����
		THROW((c = pResponse[byte_pos++]) == 6); // OBJECT IDENTIFIER
		block_len = pResponse[byte_pos++];
		byte_pos += block_len;
		// ���������� ����� � ������ � ����� messageImprint MessageImprint, �� � ���������
		THROW((c = pResponse[byte_pos++]) == 48); // ������������������
		byte_pos ++;
		// ���������� ���� � ������ � ����� MessageImprint ::= SEQUENCE, �� � ��������� ����
		THROW((c = pResponse[byte_pos++]) == 48); // ������������������
		byte_pos ++;
		// ���������� �������� ����
		THROW((c = pResponse[byte_pos++]) == 6); // OBJECT IDENTIFIER
		block_len = pResponse[byte_pos++];
		rResponseFmtd.HashAlg.CopyFromN(pResponse + byte_pos, block_len);
		byte_pos += block_len;
		// ���������� ���
		THROW((c = pResponse[byte_pos++]) == 4); // OCTET STRING
		block_len = pResponse[byte_pos++];
		rResponseFmtd.Hash.CopyFromN(pResponse + byte_pos, block_len);
		byte_pos += block_len;
		// ���������� ����� � ������ � ����� serialNumber INTEGER, �� � ��������� ����
		THROW((c = pResponse[byte_pos++]) == 2); // INTEGER
		block_len = pResponse[byte_pos++];
		byte_pos += block_len;
		// �������� ����� ��������� ������ ������� � ��������� ����
		THROW((c = pResponse[byte_pos++]) == 24); // GeneralizedTime ��������������Z
		block_len = pResponse[byte_pos++];
		LDATETIME da_tm;
		LDATE date;
		LTIME time;
		long sec = 0;
		char val[4] = "";
		size_t start_pos = byte_pos; // �������� ��������� ���������
		memzero(val, 4);
		memcpy(val, pResponse + byte_pos, 4);
		byte_pos += 4;
		date.setyear(atoi(val));
		memzero(val, 4);
		memcpy(val, pResponse + byte_pos, 2);
		byte_pos += 2;
		date.setmonth(atoi(val));
		memzero(val, 4);
		memcpy(val, pResponse + byte_pos, 2);
		byte_pos += 2;
		date.setday(atoi(val));
		memzero(val, 4);
		memcpy(val, pResponse + byte_pos, 2);
		byte_pos += 2;
		sec += atoi(val) * 3600;
		memzero(val, 4);
		memcpy(val, pResponse + byte_pos, 2);
		byte_pos += 2;
		sec += atoi(val) * 60;
		memzero(val, 4);
		memcpy(val, pResponse + byte_pos, 2);
		byte_pos += 2;
		sec += atoi(val);
		time.settotalsec(sec);
		da_tm.Set(date, time);
		rResponseFmtd.Time = da_tm;
		byte_pos = start_pos + block_len; // �������� ������� ������ ���, ������ ��� ����� ����/�������
										// ����� ���� ".0Z" (0 - ����� �����, ���� �������) ��� ������ "Z"

		// ��������� �������� ������� ��������� ������, �� � ��������� ����
		// ���� �������� ����������, ������� ��� ����� �� ���� � ������
		if((c = pResponse[byte_pos]) == 48) {
			byte_pos++;
			block_len = pResponse[byte_pos++];
			byte_pos += block_len;
		}
		// ���� Nonce ������-�� ���� ����������, ��, ��� ��� �� ��� ������� � �������, �� � ������ �� ������ ���� �����������
		THROW((c = pResponse[byte_pos++]) == 2); // INTEGER
		block_len = pResponse[byte_pos++];
		rResponseFmtd.Nonce.CopyFromN(pResponse + byte_pos, block_len);
		byte_pos += block_len;

		// ����� �������, ��� ��� ��� ���� ���
	}

	CATCHZOK;
	return ok;
}

int PPEds::GetTimeStamp(const char * pSignFileName, int signerNumber, StTspResponse & rResponse)
{
	struct Request {
		Request()
		{
			StartByte = 48; // ������� � ���, ��� ������ �������� ���������������� ���������� (SEQUENCE)
			RequestLen = 0;
			Separator1.Z().CatChar(2).CatChar(1); // ��� ������ INTEGER, ����� 1
			Version = 1; // ������ ������� V1
			Separator2.Z().CatChar(48); // ��� ������ SEQUENCE (����� ��������� ����� ������ ����� �����)
			Separator3.Z().CatChar(48); // ��� ������ SEQUENCE (����� ��������� ����� ������ ����� �����)
			Separator4.Z().CatChar(6); // ��� ������ OBJECT IDENTIFIER (����� ��������� ����� ������ ����� �����)
			HashAlgorytm = "";
			Separator5.Z().CatChar(4); // ��� ������ OBJECT STRING (����� ��������� ����� ������ ����� �����)
			Hash = "";
			Separator6.Z().CatChar(6); // ��� ������ OBJECT IDENTIFIER (����� ��������� ����� ������ ����� �����)
			Policy = "";
			Separator7.Z().CatChar(2); // ��� ������ INTEGER (����� ��������� ����� ������ ����� �����)
			Nonce = "";
			Separator8.Z().CatChar(1).CatChar(1); // ��� ������ BOOLEAN, ����� ������ 1 ���
			CertReq = 0;
		}
		void CalcLen()
		{
			Separator4.CatChar(HashAlgorytm.Len());
			Separator3.CatChar(HashAlgorytm.Len() + Separator4.Len());
			Separator5.CatChar(Hash.Len());
			Separator2.CatChar(Separator3.Len() + Separator4.Len() + HashAlgorytm.Len() + Separator5.Len() + Hash.Len());
			if(Policy.Len())
				Separator6.CatChar(Policy.Len());
			else
				Separator6 = "";
			if(Nonce.Len())
				Separator7.CatChar(Nonce.Len());
			else
				Separator7 = "";
			if(CertReq) {
				RequestLen += 2; // ���� �� ��������� ����� ������ �� �������
				RequestLen += (char)Separator8.Len();
			}
			else
				RequestLen += 1;
			RequestLen += (char)Separator1.Len();
			RequestLen += (char)Separator2.Len();
			RequestLen += (char)Separator3.Len();
			RequestLen += (char)Separator4.Len();
			RequestLen += (char)HashAlgorytm.Len();
			RequestLen += (char)Separator5.Len();
			RequestLen += (char)Hash.Len();
			if(Policy.NotEmpty()) {
				RequestLen += (char)Separator6.Len();
				RequestLen += (char)Policy.Len();
			}
			if(Nonce.NotEmpty()) {
				RequestLen += (char)Separator7.Len();
				RequestLen += (char)Nonce.Len();
			}
		}

		char   StartByte;     // ��� �������
		char   RequestLen;    // ����� �������
		SString Separator1;   // ��� ������ � �����: ������  �������
		char   Version;       // ������  �������
		SString Separator2;   // ��� ������ � �����: �������� ����������� � ��� ���
		SString Separator3;   // ��� ������ � �����: ������������� ��������� �����������, ��������� (� ����� ������ ���������� ���)
		SString Separator4;   // ��� ������ � ����� �������: ������������� ��������� �����������
		SString HashAlgorytm; // ������������� ��������� �����������
		SString Separator5;   // ��� ������ � ����� �������: ���
		SString Hash;         // ���
		SString Separator6;   // ��� ������ � �����: �������� ����������
		SString Policy;       // �������� ����������
		SString Separator7;   // ��� ������ � �����: ���� nonce
		SString Nonce;        // nonce
		SString Separator8;   // ��� ������ � �����: ����� �� web-������ ���������� ���� ����������
		char   CertReq;
	};

	int    ok = 1;
	SHttpClient http_cl;
	SString str, request;
	InetAddr inet_addr;
	TcpSocket sock;
	char resp_buf[128];
	char * p_indata = 0;
	WSADATA wsdata;
	SFile file;
	int64 file_size = 0;
	SString hash_str, hashalg_der, policy_der;
	Request req_struct;
	HCRYPTMSG h_msg = 0, stamp_msg = 0;
	BYTE * pb_stamp_data = 0; // ����� ������ �� �������, ���������� ������ ���������� � ������ �������
	DWORD  cb_stamp_data = 0;
	BYTE * pb_stamp_decoded = 0; // �� ��, ��� � pb_stamp_data, ������ � ��������������� ����
	DWORD  cb_stamp_decoded = 0;
	BYTE * pb_stamp_encoded = 0; // �������������� ���������� � ������ ������, ������� ����� ��������� � �������� �������
	DWORD  cb_stamp_encoded = 0;
	BYTE * pb_stamped_data = 0; // ������ � ������� �� ������� �������
	DWORD  cb_stamped_data = 0;
	BYTE * hash = 0;
	DWORD  hash_size = 0;
	CRYPT_ATTRIBUTE attr;
	CMSG_CTRL_ADD_SIGNER_UNAUTH_ATTR_PARA attr_para;
	CRYPT_DATA_BLOB attr_data;
	SBuffer buf(1024);

	attr_data.pbData = 0;
	attr.rgValue = 0;

	THROW_SL(file.Open(pSignFileName, SFile::mRead | SFile::mBinary));
	THROW(file.IsValid());
	file.CalcSize(&file_size);
	THROW_MEM(p_indata = new char[(uint)file_size]);
	memzero(p_indata, (size_t)file_size);
	file.ReadV(p_indata, (size_t)file_size);
	file.Close();

	THROW(h_msg = CryptMsgOpenToDecode(MY_ENCODING_TYPE, 0, 0, 0, NULL, NULL));
    // ������� � h_msg ���� � ��������
    THROW(CryptMsgUpdate(h_msg, (BYTE *)p_indata, (DWORD)file_size, TRUE));

	hash_size = 0;
	THROW(GetHash(p_indata, (DWORD)file_size, signerNumber, NULL, hash_size));
	hash = new BYTE[hash_size];
	memzero(hash, hash_size);
	THROW(GetHash(p_indata, (DWORD)file_size, signerNumber, hash, hash_size));

	// ��������� �������:
	//
	//	version                      INTEGER  { v1(1) },
	//	messageImprint               MessageImprint,
	//		--a hash algorithm OID and the hash value of the data to be
	//		--time-stamped
	//	reqPolicy             TSAPolicyId              OPTIONAL,
	//	nonce                 INTEGER                  OPTIONAL,
	//	certReq               BOOLEAN                  DEFAULT FALSE,
	//	extensions            [0] IMPLICIT Extensions  OPTIONAL  }

	// �������� ������

	// ���� � wincrypt.h ���� �������� X509_OID (��� ���-�� ��������),
	// der-����������� ������ OBJECT IDENTIFIER ����� ����������� ���,
	// ��� ���������������� ����. ��, ��� ��� � ���� ������ ������ ��������� �� ����,
	// �� �������� �������� ���� ������� ��� ����������� ObjIdfrDerEncode().
	//BYTE * encoded = 0;
	//DWORD size_encoded = 0;
	//CERT_NAME_VALUE name_val;
	//name_val.dwValueType = CERT_RDN_VISIBLE_STRING;
	//name_val.Value.cbData = sstrlen(HASH_ALG);
	//name_val.Value.pbData = (BYTE*)HASH_ALG;
	//if(!CryptEncodeObject(MY_ENCODING_TYPE, X509_OID,
	//	&name_val, NULL, &size_encoded))
	//	ShowError();
	//encoded = new BYTE[size_encoded];
	//memzero(encoded, size_encoded);
	////PKCS_7_ASN_ENCODING | X509_ASN_ENCODING
	//if(!CryptEncodeObject(X509_ASN_ENCODING, X509_ANY_STRING,
	//	&name_val, encoded, &size_encoded))
	//	ShowError();

	THROW_PP(ObjIdfrDerEncode("1.2.643.2.2.9", hashalg_der), PPERR_EDS_DERENCODEFAILED); // ������������� ��������� �����������
	hash_str.CopyFromN((char*)hash, hash_size);
	//THROW_PP(ObjIdfrDerEncode("1.2.643.2.2.38.4", policy_der), PPERR_EDS_DERENCODEFAILED); // �������� ������������ // @vmiller comment

	req_struct.HashAlgorytm.Z().Cat(hashalg_der);
	req_struct.Hash.Z().Cat(hash_str);
	req_struct.Policy.Z().Cat(policy_der);
	// ����������� nonce
	req_struct.Nonce = 0;
	for(size_t i = 0; i < 8; i++) {
		// �������� � ��������� 0 - 255
		int val = rand() % 256;
		req_struct.Nonce.CatChar(val);
	}
	req_struct.CertReq = 1;
	req_struct.CalcLen();

	request.Z().CatChar(req_struct.StartByte).CatChar(req_struct.RequestLen).Cat(req_struct.Separator1).
		CatChar(req_struct.Version).Cat(req_struct.Separator2).Cat(req_struct.Separator3).
		Cat(req_struct.Separator4).Cat(req_struct.HashAlgorytm).Cat(req_struct.Separator5).
		Cat(req_struct.Hash);
	if(req_struct.Policy.NotEmpty())
		request.Cat(req_struct.Separator6).Cat(req_struct.Policy);
	if(req_struct.Nonce.NotEmpty())
		request.Cat(req_struct.Separator7).Cat(req_struct.Nonce);
	if(req_struct.CertReq)
		request.Cat(req_struct.Separator8).CatChar(req_struct.CertReq);

	WSAStartup(MAKEWORD(1, 0), &wsdata);
	inet_addr.Set(HOST, 80);
	THROW_SL(sock.Connect(inet_addr));
	str.Z().Cat("POST ").Cat(URL).Space().Cat("HTTP/1.1\r\n");
	THROW_SL(sock.Send(str, str.Len(), NULL));
	printf("%s", (const char*)str);
	str.Z().Cat("Host: ").Cat(HOST).Cat("\r\n");
	THROW_SL(sock.Send(str, str.Len(), NULL));
	printf("%s", (const char*)str);
	str.Z().Cat("Content-Type: application/timestamp-query\r\n");
	THROW_SL(sock.Send(str, str.Len(), NULL));
	printf("%s", (const char*)str);
	str.Z().Cat("Content-Length: ").Cat((uint)request.Len()).Cat("\r\n");
	THROW_SL(sock.Send(str, str.Len(), NULL));
	printf("%s", (const char*)str);
	str.Z().Cat("\r\n");
	THROW_SL(sock.Send(str, str.Len(), NULL));
	printf("%s", (const char*)str);
	THROW_SL(sock.Send(request, request.Len(), NULL));
	printf("%s", (const char*)request);
	printf("\n");

	uint   size = SIZEOFARRAY(resp_buf);
	uint   l = SIZEOFARRAY(resp_buf);
	uint   count = 20;
	size_t recvd_size = 0, wr_pos = 0;
	Sleep(2000); // �������� ���� ������, ���� ������ ������������
	while((l == size) || (count > 0)) {
		memzero(resp_buf, sizeof(resp_buf));
		Sleep(10);
		if(sock.Recv(resp_buf, size, &recvd_size)) {
			l = recvd_size;
			buf.Write(resp_buf, recvd_size);
		}
		else
			l = 0;
		recvd_size = 0;
		count--;
	}

	size_t pos = 0;
	if(buf.GetWrOffs() != 0) {
		SString substr;
		substr.CopyFromN((const char *)buf.GetBuf(), buf.GetSize());
		substr.Search("\r\n\r\n", 0, 1, &pos); // ����� ���������� http-������ � ������ ����� - ������ ������
		file.Open("answer.tsr", SFile::mWrite|SFile::mBinary);
		THROW_PP(ParseTSAResponse((char *)buf.GetBuf() + pos + 4, rResponse), PPERR_EDS_TSPRESPPARSEFAILED);
		THROW_PP((rResponse.PKIStatus == 0) || (rResponse.PKIStatus == 1), PPERR_EDS_TSPFAILED);
        THROW_PP(req_struct.HashAlgorytm.CmpNC(rResponse.HashAlg) == 0, PPERR_EDS_TSPFAILED);
		THROW_PP(req_struct.Hash.CmpNC(rResponse.Hash) == 0, PPERR_EDS_TSPFAILED);
		THROW_PP(req_struct.Nonce.CmpNC(rResponse.Nonce) == 0, PPERR_EDS_TSPFAILED);
		// ���������, ����� ����� ������������ ������ ������ �� ���������� �� �������� (� ����� 5 �����)
		LDATETIME cur_dt_tm = getcurdatetime_();
		LTIME  five_minutes, diff_time;
		int    time_zone = gettimezone();
		ulong  seconds = cur_dt_tm.t;
		cur_dt_tm.t.settotalsec(time_zone * 60);
		cur_dt_tm.t.v += seconds;
		int diff = cur_dt_tm.d - rResponse.Time.d;
		if(diff == 1) {
			five_minutes.settotalsec(86400);
			diff = (five_minutes.hour() - rResponse.Time.t.hour() + cur_dt_tm.t.hour()) * 3600 +
				(five_minutes.minut() - rResponse.Time.t.minut() + cur_dt_tm.t.minut()) * 60 +
				(five_minutes.sec() - rResponse.Time.t.sec() + cur_dt_tm.t.sec());
			five_minutes.settotalsec(diff);
		}
		else
			diff = abs((long)cur_dt_tm.t - (long)rResponse.Time.t);
		diff_time.v = diff;
		five_minutes.settotalsec(300); // 5 �����
		THROW_PP(diff_time < five_minutes, PPERR_EDS_TSPFAILED);

		buf.SetRdOffs(0);
		file.Write((const char*)buf.GetBuf() + pos + 4, buf.GetSize() - pos - 4);
		file.Close();
	}

	// ��������� ����� � ������� �������
	// ������� �����������
	cb_stamp_data = buf.GetSize() - pos - 4 - 9; // ������ ��� ��� 9? �� ������ ��� ������ 9 ����
	// � ������ - ����� ���� �� ���� ����� ������. � ������ ��� ���� ���������������
	// ����� �������.
	THROW_MEM(pb_stamp_data = new BYTE[cb_stamp_data]);
	memzero(pb_stamp_data, cb_stamp_data);
	memcpy(pb_stamp_data, (char *)buf.GetBuf() + pos + 4 + 9, cb_stamp_data);
	THROW_PP(stamp_msg = CryptMsgOpenToDecode(MY_ENCODING_TYPE, 0, 0, 0, NULL, NULL), PPERR_EDS_MSGOPENFAILED);
    // ������� � h_msg ���� � ��������
	THROW_PP(CryptMsgUpdate(stamp_msg, pb_stamp_data, cb_stamp_data, TRUE), PPERR_EDS_MSGUPDATEFAILED);
	THROW_PP(CryptMsgGetParam(h_msg, CMSG_ENCODED_MESSAGE, 0, NULL, &cb_stamp_decoded), PPERR_EDS_TSPFAILED);
	THROW_MEM(pb_stamp_decoded = new BYTE[cb_stamp_decoded]);
	memzero(pb_stamp_decoded, cb_stamp_decoded);
	THROW_PP(CryptMsgGetParam(h_msg, CMSG_ENCODED_MESSAGE, 0, pb_stamp_decoded, &cb_stamp_decoded), PPERR_EDS_TSPFAILED);

	// ������ ���������� �������� ����������� ����������
	attr.pszObjId = szOID_KP_TIME_STAMP_SIGNING;
	attr.cValue = 1;
	attr.rgValue = 0;
	THROW_MEM(attr.rgValue = new CRYPT_ATTR_BLOB[sizeof(PCRYPT_ATTR_BLOB)]);
	attr.rgValue->cbData = cb_stamp_decoded;
	THROW_MEM(attr.rgValue->pbData = new BYTE[attr.rgValue->cbData]);
	memzero(attr.rgValue->pbData, attr.rgValue->cbData);
	memcpy(attr.rgValue->pbData, pb_stamp_decoded, attr.rgValue->cbData);
	THROW_PP(CryptEncodeObject(MY_ENCODING_TYPE, PKCS_ATTRIBUTE, &attr,
		NULL, &cb_stamp_encoded), PPERR_EDS_MSGOPENFAILED);
    THROW_MEM(pb_stamp_encoded = new BYTE[cb_stamp_encoded]);
	memzero(pb_stamp_encoded, cb_stamp_encoded);
	THROW_PP(CryptEncodeObject(MY_ENCODING_TYPE, PKCS_ATTRIBUTE, &attr,
		pb_stamp_encoded, &cb_stamp_encoded), PPERR_EDS_MSGOPENFAILED);

	// ���������� ��������� ����� � ������������� �������� �������
	THROW_MEM(attr_data.pbData = new BYTE[cb_stamp_encoded]);
	memzero(attr_data.pbData, cb_stamp_encoded);
	memcpy(attr_data.pbData, pb_stamp_encoded, cb_stamp_encoded);
	attr_data.cbData = cb_stamp_encoded;

	attr_para.cbSize = sizeof(CMSG_CTRL_ADD_SIGNER_UNAUTH_ATTR_PARA);
	attr_para.dwSignerIndex = signerNumber - 1;
	attr_para.blob = attr_data;
	THROW_PP(CryptMsgControl(h_msg, 0, CMSG_CTRL_ADD_SIGNER_UNAUTH_ATTR, &attr_para), PPERR_EDS_TSPFAILED);

	// �������� ������ ���������
    THROW_PP(CryptMsgGetParam(h_msg, CMSG_ENCODED_MESSAGE, 0, NULL, &cb_stamped_data), PPERR_EDS_TSPFAILED);
    // �������� ������ ��� ���������
	THROW_MEM(pb_stamped_data = new BYTE[cb_stamped_data]);
	memzero(pb_stamped_data, cb_stamped_data);
    // �������� ���������
    THROW_PP(CryptMsgGetParam(h_msg, CMSG_ENCODED_MESSAGE, 0, pb_stamped_data, &cb_stamped_data), PPERR_EDS_TSPFAILED);
	THROW_SL(file.Open("test_stamped.p7s", SFile::mWrite | SFile::mBinary));
	file.Write(pb_stamped_data, cb_stamped_data);
	file.Close();

	CATCHZOK;
	WSACleanup();
	if(p_indata)
		ZDELETE(p_indata);
	if(hash)
		ZDELETE(hash);
	if(h_msg)
        CryptMsgClose(h_msg);
	if(stamp_msg)
		CryptMsgClose(stamp_msg);
	if(attr_data.pbData)
		ZDELETE(attr_data.pbData);
	if(attr.rgValue) {
		if(attr.rgValue->pbData)
			ZDELETE(attr.rgValue->pbData);
		ZDELETE(attr.rgValue);
	}
	if(pb_stamp_decoded)
		ZDELETE(pb_stamp_decoded);
	if(pb_stamp_data)
		ZDELETE(pb_stamp_data);
	return ok;
}

int PPEds::GetSignFilesForDoc(const char * pFileName, StrAssocArray & rFilesLis)
{
	int    ok = 1, r = -1;
	size_t i = 0;
	SFile file;
	SFindFile find_file;
	SString path;
	SPathStruc spath(pFileName);
	spath.Nam.CatChar('*');
	spath.Ext.Z().Cat("p7s");
	spath.Merge(path);
	WIN32_FIND_DATA found_file;
	HANDLE hd;
	if((hd = FindFirstFile(path, &found_file)) == INVALID_HANDLE_VALUE) { // @unicodeproblem
		r = GetLastError();
		if((r != ERROR_FILE_NOT_FOUND) && (r != ERROR_NO_MORE_FILES)) // ���������� ������ �� ������� �� ������
			ok = 0;
	}
	else
		rFilesLis.Add(i++, found_file.cFileName); // @unicodeproblem
	if(ok == 1) {
		while(FindNextFile(hd, &found_file)) { // @unicodeproblem // ������� �� �����, ����� ������� ��������� ERROR_FILE_NOT_FOUND
			rFilesLis.Add(i++, found_file.cFileName);
		}
		if(r == -1)
			r = GetLastError();
		if((r == ERROR_FILE_NOT_FOUND) || (r == ERROR_NO_MORE_FILES)) // ��� ������� ������
			ok = 1;
		else { // ��������� ������ �������
			ok = 0;
		}
	}
	FindClose(hd);
	return ok;
}

int PPEds::GetSignFileAndSignNames(int posInList, const char * pFileName, SString & rSignFileName)
{
	int    ok = 1, count = 0;
	StrAssocArray sign_files_list;
	SString signer_name;
	THROW(GetSignFilesForDoc(pFileName, sign_files_list));
	for(size_t i = 1; i <= sign_files_list.getCount(); i++) {
		THROW(GetSignsCount(sign_files_list.Get(i-1).Txt, count));
		if((posInList - (int)i) <= count)
			rSignFileName.CopyFrom(sign_files_list.Get(i-1).Txt);
	}
	CATCHZOK;
	return ok;
}
//
//int PPEds::GetSignersInDoc(const char * pFileName, StrAssocArray & rSignersArr)
//{
//	int ok = 1, sign_count = 0;
//	SString signer_name;
//
//	THROW(GetSignsCount(pFileName, sign_count));
//	if(sign_count > 0) {
//		for(size_t i = 1; i < (size_t)sign_count + 1; i++) {
//			THROW(GetSignerNameByNumber(pFileName, i, signer_name = 0));
//			rSignersArr.Add(i - 1, signer_name, 1);
//			//signer_name.ToOem();
//			//cout << i << " - " << signer_name << endl; // @vmiller �� ����� ����������
//		}
//	}
//	else {
//		//cout << "Document is not signed" << endl;
//		PPOutputMessage("Document is not signed", mfYes | mfLargeBox);
//		ok = -1;
//	}
//
//	CATCH
//		ok = 0;
//	ENDCATCH;
//	return ok;
//}
