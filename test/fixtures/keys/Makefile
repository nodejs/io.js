all: \
  ca1-cert.pem \
  ca2-cert.pem \
  ca2-crl.pem \
  ca3-cert.pem \
  ca4-cert.pem \
  ca5-cert.pem \
  ca6-cert.pem \
  agent1-cert.pem \
  agent1.pfx \
  agent2-cert.pem \
  agent3-cert.pem \
  agent4-cert.pem \
  agent5-cert.pem \
  agent6-cert.pem \
  agent6.pfx \
  agent7-cert.pem \
  agent8-cert.pem \
  agent9-cert.pem \
  agent10-cert.pem \
  agent10.pfx \
  ec10-cert.pem \
  ec10.pfx \
  dh512.pem \
  dh1024.pem \
  dh2048.pem \
  dh3072.pem \
  dherror.pem \
  dh_private.pem \
  dh_public.pem \
  dsa_params.pem \
  dsa_private.pem \
  dsa_private_encrypted.pem \
  dsa_private_pkcs8.pem \
  dsa_public.pem \
  dsa1025.pem \
  dsa_private_1025.pem \
  dsa_private_encrypted_1025.pem \
  dsa_public_1025.pem \
  ec-cert.pem \
  ec.pfx \
  fake-cnnic-root-cert.pem \
  intermediate-ca-cert.pem \
  intermediate-ca-key.pem \
  leaf-from-intermediate-cert.pem \
  leaf-from-intermediate-key.pem \
  non-trusted-intermediate-ca-cert.pem \
  non-trusted-intermediate-ca-key.pem \
  non-trusted-leaf-from-intermediate-cert.pem \
  non-trusted-leaf-from-intermediate-key.pem \
  rsa_private.pem \
  rsa_private_encrypted.pem \
  rsa_private_pkcs8.pem \
  rsa_private_pkcs8_bad.pem \
  rsa_public.pem \
  rsa_ca.crt \
  rsa_cert.crt \
  rsa_cert.pfx \
  rsa_public_sha1_signature_signedby_rsa_private.sha1 \
  rsa_public_sha1_signature_signedby_rsa_private_pkcs8.sha1 \
  rsa_private_b.pem \
  I_AM_THE_WALRUS_sha256_signature_signedby_rsa_private_b.sha256 \
  rsa_public_b.pem \
  rsa_cert_foafssl_b.crt \
  rsa_cert_foafssl_b.modulus \
  rsa_cert_foafssl_b.exponent \
  rsa_spkac.spkac \
  rsa_spkac_invalid.spkac \
  rsa_private_2048.pem \
  rsa_private_4096.pem \
  rsa_public_2048.pem \
  rsa_public_4096.pem \
  rsa_pss_private_2048.pem \
  rsa_pss_private_2048_sha256_sha256_16.pem \
  rsa_pss_private_2048_sha512_sha256_20.pem \
  rsa_pss_private_2048_sha1_sha1_20.pem \
  rsa_pss_public_2048.pem \
  rsa_pss_public_2048_sha256_sha256_16.pem \
  rsa_pss_public_2048_sha512_sha256_20.pem \
  rsa_pss_public_2048_sha1_sha1_20.pem \
  ed25519_private.pem \
  ed25519_public.pem \
  x25519_private.pem \
  x25519_public.pem \
  ed448_private.pem \
  ed448_public.pem \
  x448_private.pem \
  x448_public.pem \
  ec_p256_private.pem \
  ec_p256_public.pem \
  ec_p384_private.pem \
  ec_p384_public.pem \
  ec_p521_private.pem \
  ec_p521_public.pem \
  ec_secp256k1_private.pem \
  ec_secp256k1_public.pem \
  incorrect_san_correct_subject-cert.pem \
  incorrect_san_correct_subject-key.pem \
  irrelevant_san_correct_subject-cert.pem \
  irrelevant_san_correct_subject-key.pem \

#
# Create Certificate Authority: ca1
# ('password' is used for the CA password.)
#
ca1-cert.pem: ca1.cnf
	openssl req -new -x509 -days 99999 -config ca1.cnf -keyout ca1-key.pem -out ca1-cert.pem

#
# Create Certificate Authority: ca2
# ('password' is used for the CA password.)
#
ca2-cert.pem: ca2.cnf
	openssl req -new -x509 -days 99999 -config ca2.cnf -keyout ca2-key.pem -out ca2-cert.pem
	echo '01' > ca2-serial
	touch ca2-database.txt

#
# Create Subordinate Certificate Authority: ca3 issued by ca1
# ('password' is used for the CA password.)
#
ca3-key.pem:
	openssl genrsa -out ca3-key.pem 2048

ca3-csr.pem: ca3.cnf ca3-key.pem
	openssl req -new \
		-extensions v3_ca \
		-config ca3.cnf \
		-key ca3-key.pem \
		-out ca3-csr.pem

ca3-cert.pem: ca3-csr.pem ca3-key.pem ca3.cnf ca1-cert.pem ca1-key.pem
	openssl x509 -req \
		-extfile ca3.cnf \
		-extensions v3_ca \
		-days 99999 \
		-passin "pass:password" \
		-in ca3-csr.pem \
		-CA ca1-cert.pem \
		-CAkey ca1-key.pem \
		-CAcreateserial \
		-out ca3-cert.pem

#
# Create Subordinate Certificate Authority: ca4 issued by ca2
# ('password' is used for the CA password.)
#
ca4-key.pem:
	openssl genrsa -out ca4-key.pem 2048

ca4-csr.pem: ca4.cnf ca4-key.pem
	openssl req -new \
		-extensions v3_ca \
		-config ca4.cnf \
		-key ca4-key.pem \
		-out ca4-csr.pem

ca4-cert.pem: ca4-csr.pem ca4-key.pem ca4.cnf ca2-cert.pem ca2-key.pem
	openssl x509 -req \
		-extfile ca4.cnf \
		-extensions v3_ca \
		-days 99999 \
		-passin "pass:password" \
		-in ca4-csr.pem \
		-CA ca2-cert.pem \
		-CAkey ca2-key.pem \
		-CAcreateserial \
		-out ca4-cert.pem

#
# Create Certificate Authority: ca5 with ECC
# ('password' is used for the CA password.)
#
ca5-key.pem:
	openssl ecparam -genkey -out ca5-key.pem -name prime256v1

ca5-csr.pem: ca5.cnf ca5-key.pem
	openssl req -new \
		-config ca5.cnf \
		-key ca5-key.pem \
		-out ca5-csr.pem

ca5-cert.pem: ca5.cnf ca5-key.pem ca5-csr.pem
	openssl x509 -req \
		-extfile ca5.cnf \
		-extensions v3_ca \
		-days 99999 \
		-passin "pass:password" \
		-in ca5-csr.pem \
		-signkey ca5-key.pem \
		-out ca5-cert.pem

#
# Create Subordinate Certificate Authority: ca6 issued by ca5 with ECC
# ('password' is used for the CA password.)
#
ca6-key.pem:
	openssl ecparam -genkey -out ca6-key.pem -name prime256v1

ca6-csr.pem: ca6.cnf ca6-key.pem
	openssl req -new \
		-extensions v3_ca \
		-config ca6.cnf \
		-key ca6-key.pem \
		-out ca6-csr.pem

ca6-cert.pem: ca6-csr.pem ca6-key.pem ca6.cnf ca5-cert.pem ca5-key.pem
	openssl x509 -req \
		-extfile ca6.cnf \
		-extensions v3_ca \
		-days 99999 \
		-passin "pass:password" \
		-in ca6-csr.pem \
		-CA ca5-cert.pem \
		-CAkey ca5-key.pem \
		-CAcreateserial \
		-out ca6-cert.pem

#
# Create Fake CNNIC Root Certificate Authority: fake-cnnic-root
#

fake-cnnic-root-key.pem:
	openssl genrsa -out fake-cnnic-root-key.pem 2048

fake-cnnic-root-cert.pem: fake-cnnic-root.cnf fake-cnnic-root-key.pem
	openssl req -x509 -new \
	        -key fake-cnnic-root-key.pem \
	        -days 99999 \
	        -out fake-cnnic-root-cert.pem \
	        -config fake-cnnic-root.cnf

#
# Create Fake StartCom Root Certificate Authority: fake-startcom-root
#
fake-startcom-root-key.pem:
	openssl genrsa -out fake-startcom-root-key.pem 2048

fake-startcom-root-cert.pem: fake-startcom-root.cnf \
	fake-startcom-root-key.pem
	openssl req -new -x509 -days 99999 -config \
	fake-startcom-root.cnf -key fake-startcom-root-key.pem -out \
	fake-startcom-root-cert.pem
	echo '01' > fake-startcom-root-serial
	touch fake-startcom-root-database.txt


intermediate-ca-key.pem:
	openssl genrsa -out intermediate.key 2048

intermediate-ca-cert.pem: intermediate-ca-key.pem
	openssl req -new \
			-sha256 \
			-nodes \
			-key intermediate.key  \
			-subj "/C=US/ST=CA/L=SF/O=NODEJS/CN=NodeJS-Test-Intermediate-CA" \
			-out test-intermediate-ca.csr

	openssl x509 -req \
			-extensions v3_ca \
			-extfile fake-startcom-root.cnf \
			-in test-intermediate-ca.csr \
			-CA fake-startcom-root-cert.pem \
			-CAkey fake-startcom-root-key.pem \
			-CAcreateserial \
			-out intermediate-ca.pem \
			-days 99999 \
			-sha256
	rm -f test-intermediate-ca.csr

leaf-from-intermediate-key.pem:
	openssl genrsa -out leaf-from-intermediate-key.pem 2048

leaf-from-intermediate-cert.pem: leaf-from-intermediate-key.pem
	openssl genrsa -out leaf-from-intermediate-key.pem 2048
	openssl req -new \
			-sha256 \
			-nodes \
			-key leaf-from-intermediate-key.pem \
			-addext "subjectAltName = DNS:localhost" \
			-subj "/C=US/ST=CA/L=SF/O=NODEJS/CN=localhost" \
			-out leaf-from-intermediate-cert.csr
	openssl x509 -req \
			-in leaf-from-intermediate-cert.csr \
			-CA intermediate-ca.pem \
			-CAkey intermediate.key \
			-CAcreateserial \
			-out leaf-from-intermediate-cert.pem \
			-days 99999 \
			-copy_extensions copy \
			-sha256

	rm -f leaf-from-intermediate-cert.csr

non-trusted-intermediate-ca-key.pem:
	openssl genrsa -out non-trusted-intermediate.key 2048

non-trusted-intermediate-ca-cert.pem: non-trusted-intermediate-ca-key.pem
	openssl req -new \
			-sha256 \
			-nodes \
			-key non-trusted-intermediate.key  \
			-subj "/C=US/ST=CA/L=SF/O=NODEJS/CN=NodeJS-Non-Trusted-Test-Intermediate-CA" \
			-out non-trusted-test-intermediate-ca.csr

	openssl x509 -req \
			-extensions v3_ca \
			-extfile fake-startcom-root.cnf \
			-in non-trusted-test-intermediate-ca.csr \
			-passin "pass:password" \
			-CA ca1-cert.pem \
			-CAkey ca1-key.pem \
			-CAcreateserial \
			-out non-trusted-intermediate-ca.pem \
			-days 99999 \
			-sha256
	rm -f non-trusted-test-intermediate-ca.csr

non-trusted-leaf-from-intermediate-key.pem:
	openssl genrsa -out non-trusted-leaf-from-intermediate-key.pem 2048

non-trusted-leaf-from-intermediate-cert.pem: non-trusted-leaf-from-intermediate-key.pem
	openssl genrsa -out non-trusted-leaf-from-intermediate-key.pem 2048
	openssl req -new \
			-sha256 \
			-nodes \
			-key non-trusted-leaf-from-intermediate-key.pem \
			-addext "subjectAltName = DNS:localhost" \
			-subj "/C=US/ST=CA/L=SF/O=NODEJS/CN=localhost" \
			-out non-trusted-leaf-from-intermediate-cert.csr
	openssl x509 -req \
			-in non-trusted-leaf-from-intermediate-cert.csr \
			-CA non-trusted-intermediate-ca.pem \
			-CAkey non-trusted-intermediate.key \
			-CAcreateserial \
			-out non-trusted-leaf-from-intermediate-cert.pem \
			-days 99999 \
			-copy_extensions copy \
			-sha256

	rm -f non-trusted-leaf-from-intermediate-cert.csr

#
# agent1 is signed by ca1.
#

agent1-key.pem:
	openssl genrsa -out agent1-key.pem 2048

agent1-csr.pem: agent1.cnf agent1-key.pem
	openssl req -new -config agent1.cnf -key agent1-key.pem -out agent1-csr.pem

agent1-cert.pem: agent1-csr.pem ca1-cert.pem ca1-key.pem
	openssl x509 -req \
		-extfile agent1.cnf \
		-extensions v3_ca \
		-days 99999 \
		-passin "pass:password" \
		-in agent1-csr.pem \
		-CA ca1-cert.pem \
		-CAkey ca1-key.pem \
		-CAcreateserial \
		-out agent1-cert.pem

agent1.pfx: agent1-cert.pem agent1-key.pem ca1-cert.pem
	openssl pkcs12 -export \
		-descert \
		-in agent1-cert.pem \
		-inkey agent1-key.pem \
		-certfile ca1-cert.pem \
		-out agent1.pfx \
		-password pass:sample

agent1-verify: agent1-cert.pem ca1-cert.pem
	openssl verify -CAfile ca1-cert.pem agent1-cert.pem


#
# agent2 has a self signed cert
#
# Generate new private key
agent2-key.pem:
	openssl genrsa -out agent2-key.pem 2048

# Create a Certificate Signing Request for the key
agent2-csr.pem: agent2-key.pem agent2.cnf
	openssl req -new -config agent2.cnf -key agent2-key.pem -out agent2-csr.pem

# Create a Certificate for the agent.
agent2-cert.pem: agent2-csr.pem agent2-key.pem
	openssl x509 -req \
		-days 99999 \
		-in agent2-csr.pem \
		-signkey agent2-key.pem \
		-out agent2-cert.pem

agent2-verify: agent2-cert.pem
	openssl verify -CAfile agent2-cert.pem agent2-cert.pem

#
# agent3 is signed by ca2.
#

agent3-key.pem:
	openssl genrsa -out agent3-key.pem 2048

agent3-csr.pem: agent3.cnf agent3-key.pem
	openssl req -new -config agent3.cnf -key agent3-key.pem -out agent3-csr.pem

agent3-cert.pem: agent3-csr.pem ca2-cert.pem ca2-key.pem
	openssl x509 -req \
		-days 99999 \
		-passin "pass:password" \
		-in agent3-csr.pem \
		-CA ca2-cert.pem \
		-CAkey ca2-key.pem \
		-CAcreateserial \
		-out agent3-cert.pem

agent3-verify: agent3-cert.pem ca2-cert.pem
	openssl verify -CAfile ca2-cert.pem agent3-cert.pem


#
# agent4 is signed by ca2 (client cert)
#

agent4-key.pem:
	openssl genrsa -out agent4-key.pem 2048

agent4-csr.pem: agent4.cnf agent4-key.pem
	openssl req -new -config agent4.cnf -key agent4-key.pem -out agent4-csr.pem

agent4-cert.pem: agent4-csr.pem ca2-cert.pem ca2-key.pem
	openssl x509 -req \
		-days 99999 \
		-passin "pass:password" \
		-in agent4-csr.pem \
		-CA ca2-cert.pem \
		-CAkey ca2-key.pem \
		-CAcreateserial \
		-extfile agent4.cnf \
		-extensions ext_key_usage \
		-out agent4-cert.pem

agent4-verify: agent4-cert.pem ca2-cert.pem
	openssl verify -CAfile ca2-cert.pem agent4-cert.pem

#
# Make CRL with agent4 being rejected
#
ca2-crl.pem: ca2-key.pem ca2-cert.pem ca2.cnf agent4-cert.pem
	openssl ca -revoke agent4-cert.pem \
		-keyfile ca2-key.pem \
		-cert ca2-cert.pem \
		-config ca2.cnf \
		-passin 'pass:password'
	openssl ca \
		-keyfile ca2-key.pem \
		-cert ca2-cert.pem \
		-config ca2.cnf \
		-gencrl \
		-out ca2-crl.pem \
		-passin 'pass:password'

#
# agent5 is signed by ca2 (client cert)
#

agent5-key.pem:
	openssl genrsa -out agent5-key.pem 2048

agent5-csr.pem: agent5.cnf agent5-key.pem
	openssl req -new -config agent5.cnf -key agent5-key.pem -out agent5-csr.pem

agent5-cert.pem: agent5-csr.pem ca2-cert.pem ca2-key.pem
	openssl x509 -req \
		-days 99999 \
		-passin "pass:password" \
		-in agent5-csr.pem \
		-CA ca2-cert.pem \
		-CAkey ca2-key.pem \
		-CAcreateserial \
		-extfile agent5.cnf \
		-extensions ext_key_usage \
		-out agent5-cert.pem

agent5-verify: agent5-cert.pem ca2-cert.pem
	openssl verify -CAfile ca2-cert.pem agent5-cert.pem

#
# agent6 is a client RSA cert signed by ca3
#

agent6-key.pem:
	openssl genrsa -out agent6-key.pem 2048

agent6-csr.pem: agent6.cnf agent6-key.pem
	openssl req -new -config agent6.cnf -key agent6-key.pem -out agent6-csr.pem

agent6-cert.pem: agent6-csr.pem ca3-cert.pem ca3-key.pem
	openssl x509 -req \
		-days 99999 \
		-passin "pass:password" \
		-in agent6-csr.pem \
		-CA ca3-cert.pem \
		-CAkey ca3-key.pem \
		-CAcreateserial \
		-extfile agent6.cnf \
		-out agent6-cert.pem
	cat ca3-cert.pem >> agent6-cert.pem

agent6-verify: agent6-cert.pem ca3-cert.pem ca1-cert.pem
	openssl verify -trusted ca1-cert.pem -untrusted ca3-cert.pem agent6-cert.pem

agent6.pfx: agent6-cert.pem agent6-key.pem ca1-cert.pem
	openssl pkcs12 -export \
		-descert \
		-in agent6-cert.pem \
		-inkey agent6-key.pem \
		-certfile ca1-cert.pem \
		-out agent6.pfx \
		-password pass:sample

#
# agent7 is signed by fake-cnnic-root.
#

agent7-key.pem:
	openssl genrsa -out agent7-key.pem 2048

agent7-csr.pem: agent1.cnf agent7-key.pem
	openssl req -new -config agent7.cnf -key agent7-key.pem -out agent7-csr.pem

agent7-cert.pem: agent7-csr.pem fake-cnnic-root-cert.pem fake-cnnic-root-key.pem
	openssl x509 -req \
		-extfile agent7.cnf \
		-days 99999 \
		-passin "pass:password" \
		-in agent7-csr.pem \
		-CA fake-cnnic-root-cert.pem \
		-CAkey fake-cnnic-root-key.pem \
		-CAcreateserial \
		-out agent7-cert.pem

agent7-verify: agent7-cert.pem fake-cnnic-root-cert.pem
	openssl verify -CAfile fake-cnnic-root-cert.pem agent7-cert.pem

#
# agent8 is signed by fake-startcom-root with notBefore
# of Oct 20 23:59:59 2016 GMT
#

agent8-key.pem:
	openssl genrsa -out agent8-key.pem 2048

agent8-csr.pem: agent8.cnf agent8-key.pem
	openssl req -new -config agent8.cnf -key agent8-key.pem \
	-out agent8-csr.pem

agent8-cert.pem: agent8-csr.pem fake-startcom-root-cert.pem fake-startcom-root-key.pem
	openssl ca \
		-config fake-startcom-root.cnf \
		-keyfile fake-startcom-root-key.pem \
		-cert fake-startcom-root-cert.pem \
		-batch \
		-days 99999 \
		-passin "pass:password" \
		-in agent8-csr.pem \
		-startdate 161020235959Z \
		-notext -out agent8-cert.pem


agent8-verify: agent8-cert.pem fake-startcom-root-cert.pem
	openssl verify -CAfile fake-startcom-root-cert.pem \
	agent8-cert.pem


#
# agent9 is signed by fake-startcom-root with notBefore
# of Oct 21 00:00:01 2016 GMT
#
agent9-key.pem:
	openssl genrsa -out agent9-key.pem 2048

agent9-csr.pem: agent9.cnf agent9-key.pem
	openssl req -new -config agent9.cnf -key agent9-key.pem \
	-out agent9-csr.pem


agent9-cert.pem: agent9-csr.pem
	openssl ca \
		-config fake-startcom-root.cnf \
		-keyfile fake-startcom-root-key.pem \
		-cert fake-startcom-root-cert.pem \
		-batch \
		-days 99999 \
		-passin "pass:password" \
		-in agent9-csr.pem \
		-startdate 20161021000001Z \
		-notext -out agent9-cert.pem

# agent10 is a server RSA cert signed by ca4 for agent10.example.com
#

agent10-key.pem:
	openssl genrsa -out agent10-key.pem 2048

agent10-csr.pem: agent10.cnf agent10-key.pem
	openssl req -new -config agent10.cnf -key agent10-key.pem -out agent10-csr.pem

agent10-cert.pem: agent10-csr.pem ca4-cert.pem ca4-key.pem
	openssl x509 -req \
		-days 99999 \
		-passin "pass:password" \
		-in agent10-csr.pem \
		-CA ca4-cert.pem \
		-CAkey ca4-key.pem \
		-CAcreateserial \
		-extfile agent10.cnf \
		-out agent10-cert.pem
	cat ca4-cert.pem >> agent10-cert.pem

agent10-verify: agent10-cert.pem ca4-cert.pem ca2-cert.pem
	openssl verify -trusted ca2-cert.pem -untrusted ca4-cert.pem agent10-cert.pem

agent10.pfx: agent10-cert.pem agent10-key.pem ca1-cert.pem
	openssl pkcs12 -export \
		-descert \
		-in agent10-cert.pem \
		-inkey agent10-key.pem \
		-certfile ca1-cert.pem \
		-out agent10.pfx \
		-password pass:sample

#
# ec10 is a server EC cert signed by ca6 for agent10.example.com
#

ec10-key.pem:
	openssl ecparam -genkey -out ec10-key.pem -name prime256v1

ec10-csr.pem: ec10-key.pem
	openssl req -new -config agent10.cnf -key ec10-key.pem -out ec10-csr.pem

ec10-cert.pem: ec10-csr.pem ca6-cert.pem ca6-key.pem
	openssl x509 -req \
		-days 99999 \
		-passin "pass:password" \
		-in ec10-csr.pem \
		-CA ca6-cert.pem \
		-CAkey ca6-key.pem \
		-CAcreateserial \
		-extfile agent10.cnf \
		-out ec10-cert.pem
	cat ca6-cert.pem >> ec10-cert.pem

ec10-verify: ec10-cert.pem ca6-cert.pem ca5-cert.pem
	openssl verify -trusted ca5-cert.pem -untrusted ca6-cert.pem ec10-cert.pem

ec10.pfx: ec10-cert.pem ec10-key.pem ca6-cert.pem
	openssl pkcs12 -export \
		-descert \
		-in ec10-cert.pem \
		-inkey ec10-key.pem \
		-certfile ca6-cert.pem \
		-out ec10.pfx \
		-password pass:sample


#
# ec is a self-signed EC cert for CN "agent2"
#
ec-key.pem:
	openssl ecparam -genkey -out ec-key.pem -name prime256v1

ec-csr.pem: ec-key.pem
	openssl req -new -config ec.cnf -key ec-key.pem -out ec-csr.pem

ec-cert.pem: ec-csr.pem ec-key.pem
	openssl x509 -req \
		-days 99999 \
		-in ec-csr.pem \
		-signkey ec-key.pem \
		-out ec-cert.pem

ec.pfx: ec-cert.pem ec-key.pem
	openssl pkcs12 -export \
		-descert \
		-in ec-cert.pem \
		-inkey ec-key.pem \
		-out ec.pfx \
		-password pass:

dh512.pem:
	openssl dhparam -out dh512.pem 512

dh1024.pem:
	openssl dhparam -out dh1024.pem 1024

dh2048.pem:
	openssl dhparam -out dh2048.pem 2048

dh3072.pem:
	openssl dhparam -out dh3072.pem 3072

dherror.pem: dh1024.pem
	sed 's/^[^-].*/AAAAAAAAAA/g' dh1024.pem > dherror.pem

dh_private.pem:
	openssl genpkey -algorithm dh -out dh_private.pem -pkeyopt dh_param:ffdhe2048

dh_public.pem: dh_private.pem
	openssl pkey -in dh_private.pem -pubout -out dh_public.pem

dsa_params.pem:
	openssl dsaparam -out dsa_params.pem 2048

dsa_private.pem: dsa_params.pem
	openssl gendsa -out dsa_private.pem dsa_params.pem

dsa_private_encrypted.pem: dsa_private.pem
	openssl dsa -aes256 -in dsa_private.pem -passout 'pass:password' -out dsa_private_encrypted.pem

dsa_private_pkcs8.pem: dsa_private.pem
	openssl pkcs8 -topk8 -inform PEM -outform PEM -in dsa_private.pem -out dsa_private_pkcs8.pem -nocrypt

dsa_public.pem: dsa_private.pem
	openssl dsa -in dsa_private.pem -pubout -out dsa_public.pem

dsa1025.pem:
	openssl dsaparam -out dsa1025.pem 1025

dsa_private_1025.pem:
	openssl gendsa -out dsa_private_1025.pem dsa1025.pem

dsa_private_encrypted_1025.pem:
	openssl pkcs8 -in dsa_private_1025.pem -topk8 -passout 'pass:secret' -out dsa_private_encrypted_1025.pem

dsa_public_1025.pem:
	openssl dsa -in dsa_private_1025.pem -pubout -out dsa_public_1025.pem

rsa_private.pem:
	openssl genrsa -out rsa_private.pem 2048

rsa_private_encrypted.pem: rsa_private.pem
	openssl rsa -aes256 -in rsa_private.pem -passout 'pass:password' -out rsa_private_encrypted.pem

rsa_private_pkcs8.pem: rsa_private.pem
	openssl pkcs8 -topk8 -inform PEM -outform PEM -in rsa_private.pem -out rsa_private_pkcs8.pem -nocrypt

rsa_private_pkcs8_bad.pem: rsa_private_pkcs8.pem
	sed 's/PRIVATE/RSA PRIVATE/g' rsa_private_pkcs8.pem > rsa_private_pkcs8_bad.pem

rsa_public.pem: rsa_private.pem
	openssl rsa -in rsa_private.pem -pubout -out rsa_public.pem

rsa_cert.crt: rsa_private.pem
	openssl req -new -x509 -days 99999 -key rsa_private.pem -config rsa_cert.cnf -out rsa_cert.crt

rsa_cert.pfx: rsa_cert.crt
	openssl pkcs12 -export -descert -passout 'pass:sample' -inkey rsa_private.pem -in rsa_cert.crt -out rsa_cert.pfx

rsa_ca.crt: rsa_cert.crt
	cp rsa_cert.crt rsa_ca.crt

rsa_public_sha1_signature_signedby_rsa_private.sha1: rsa_public.pem rsa_private.pem
	openssl dgst -sha1 -sign rsa_private.pem -out rsa_public_sha1_signature_signedby_rsa_private.sha1 rsa_public.pem

rsa_public_sha1_signature_signedby_rsa_private_pkcs8.sha1: rsa_public.pem rsa_private_pkcs8.pem
	openssl dgst -sha1 -sign rsa_private_pkcs8.pem -out rsa_public_sha1_signature_signedby_rsa_private_pkcs8.sha1 rsa_public.pem

rsa_private_b.pem:
	openssl genrsa -out rsa_private_b.pem 2048

I_AM_THE_WALRUS_sha256_signature_signedby_rsa_private_b.sha256: rsa_private_b.pem
	echo -n "I AM THE WALRUS" | openssl dgst -sha256 -sign rsa_private_b.pem -out I_AM_THE_WALRUS_sha256_signature_signedby_rsa_private_b.sha256

rsa_public_b.pem: rsa_private_b.pem
	openssl rsa -in rsa_private_b.pem -pubout -out rsa_public_b.pem

# The following 'foafssl' cert is used in test/parallel/test-https-foafssl.js.
# It requires a SAN like 'http://example.com/#me'. More info here:
# https://www.w3.org/wiki/Foaf+ssl
rsa_cert_foafssl_b.crt: rsa_private_b.pem
	openssl req -new -x509 -days 99999 -config rsa_cert_foafssl_b.cnf -key rsa_private_b.pem -out rsa_cert_foafssl_b.crt

# The 'modulus=' in the output must be stripped out
rsa_cert_foafssl_b.modulus: rsa_cert_foafssl_b.crt
	openssl x509 -modulus -in rsa_cert_foafssl_b.crt -noout | cut -c 9- > rsa_cert_foafssl_b.modulus

# Have to parse out the hex exponent
rsa_cert_foafssl_b.exponent: rsa_cert_foafssl_b.crt
	openssl x509 -in  rsa_cert_foafssl_b.crt -text | grep -o 'Exponent:.*' | sed 's/\(.*(\|).*\)//g' > rsa_cert_foafssl_b.exponent

# openssl outputs `SPKAC=[SPKAC]`. That prefix needs to be removed to work with node
rsa_spkac.spkac: rsa_private.pem
	openssl spkac -key rsa_private.pem -challenge this-is-a-challenge | cut -c 7- > rsa_spkac.spkac

# cutting characters from the start to invalidate the spkac
rsa_spkac_invalid.spkac: rsa_spkac.spkac
	cat rsa_spkac.spkac | cut -c 5- > rsa_spkac_invalid.spkac

rsa_private_2048.pem:
	openssl genrsa -out rsa_private_2048.pem 2048

rsa_private_4096.pem:
	openssl genrsa -out rsa_private_4096.pem 4096

rsa_public_2048.pem: rsa_private_2048.pem
	openssl rsa -in rsa_private_2048.pem -pubout -out rsa_public_2048.pem

rsa_public_4096.pem: rsa_private_4096.pem
	openssl rsa -in rsa_private_4096.pem -pubout -out rsa_public_4096.pem

rsa_pss_private_2048.pem:
	openssl genpkey -algorithm RSA-PSS -pkeyopt rsa_keygen_bits:2048 -pkeyopt rsa_keygen_pubexp:65537 -out rsa_pss_private_2048.pem

rsa_pss_private_2048_sha256_sha256_16.pem:
	openssl genpkey -algorithm RSA-PSS -pkeyopt rsa_keygen_bits:2048 -pkeyopt rsa_keygen_pubexp:65537 -pkeyopt rsa_pss_keygen_md:sha256 -pkeyopt rsa_pss_keygen_mgf1_md:sha256 -pkeyopt rsa_pss_keygen_saltlen:16 -out rsa_pss_private_2048_sha256_sha256_16.pem

rsa_pss_private_2048_sha512_sha256_20.pem:
	openssl genpkey -algorithm RSA-PSS -pkeyopt rsa_keygen_bits:2048 -pkeyopt rsa_keygen_pubexp:65537 -pkeyopt rsa_pss_keygen_md:sha512 -pkeyopt rsa_pss_keygen_mgf1_md:sha256 -pkeyopt rsa_pss_keygen_saltlen:20 -out rsa_pss_private_2048_sha512_sha256_20.pem

rsa_pss_private_2048_sha1_sha1_20.pem:
	openssl genpkey -algorithm RSA-PSS -pkeyopt rsa_keygen_bits:2048 -pkeyopt rsa_keygen_pubexp:65537 -pkeyopt rsa_pss_keygen_md:sha1 -pkeyopt rsa_pss_keygen_mgf1_md:sha1 -pkeyopt rsa_pss_keygen_saltlen:20 -out rsa_pss_private_2048_sha1_sha1_20.pem

rsa_pss_public_2048.pem: rsa_pss_private_2048.pem
	openssl pkey -in rsa_pss_private_2048.pem -pubout -out rsa_pss_public_2048.pem

rsa_pss_public_2048_sha256_sha256_16.pem: rsa_pss_private_2048_sha256_sha256_16.pem
	openssl pkey -in rsa_pss_private_2048_sha256_sha256_16.pem -pubout -out rsa_pss_public_2048_sha256_sha256_16.pem

rsa_pss_public_2048_sha512_sha256_20.pem: rsa_pss_private_2048_sha512_sha256_20.pem
	openssl pkey -in rsa_pss_private_2048_sha512_sha256_20.pem -pubout -out rsa_pss_public_2048_sha512_sha256_20.pem

rsa_pss_public_2048_sha1_sha1_20.pem: rsa_pss_private_2048_sha1_sha1_20.pem
	openssl pkey -in rsa_pss_private_2048_sha1_sha1_20.pem -pubout -out rsa_pss_public_2048_sha1_sha1_20.pem

ed25519_private.pem:
	openssl genpkey -algorithm ED25519 -out ed25519_private.pem

ed25519_public.pem: ed25519_private.pem
	openssl pkey -in ed25519_private.pem -pubout -out ed25519_public.pem

x25519_private.pem:
	openssl genpkey -algorithm x25519 -out x25519_private.pem

x25519_public.pem: x25519_private.pem
	openssl pkey -in x25519_private.pem -pubout -out x25519_public.pem

ed448_private.pem:
	openssl genpkey -algorithm ed448 -out ed448_private.pem

ed448_public.pem: ed448_private.pem
	openssl pkey -in ed448_private.pem -pubout -out ed448_public.pem

x448_private.pem:
	openssl genpkey -algorithm x448 -out x448_private.pem

x448_public.pem: x448_private.pem
	openssl pkey -in x448_private.pem -pubout -out x448_public.pem

ec_p256_private.pem:
	openssl ecparam -name prime256v1 -genkey -noout -out sec1_ec_p256_private.pem
	openssl pkcs8 -topk8 -nocrypt -in sec1_ec_p256_private.pem -out ec_p256_private.pem
	rm sec1_ec_p256_private.pem

ec_p256_public.pem: ec_p256_private.pem
	openssl ec -in ec_p256_private.pem -pubout -out ec_p256_public.pem

ec_p384_private.pem:
	openssl ecparam -name secp384r1 -genkey -noout -out sec1_ec_p384_private.pem
	openssl pkcs8 -topk8 -nocrypt -in sec1_ec_p384_private.pem -out ec_p384_private.pem
	rm sec1_ec_p384_private.pem

ec_p384_public.pem: ec_p384_private.pem
	openssl ec -in ec_p384_private.pem -pubout -out ec_p384_public.pem

ec_p521_private.pem:
	openssl ecparam -name secp521r1 -genkey -noout -out sec1_ec_p521_private.pem
	openssl pkcs8 -topk8 -nocrypt -in sec1_ec_p521_private.pem -out ec_p521_private.pem
	rm sec1_ec_p521_private.pem

ec_p521_public.pem: ec_p521_private.pem
	openssl ec -in ec_p521_private.pem -pubout -out ec_p521_public.pem

ec_secp256k1_private.pem:
	openssl ecparam -name secp256k1 -genkey -noout -out sec1_ec_secp256k1_private.pem
	openssl pkcs8 -topk8 -nocrypt -in sec1_ec_secp256k1_private.pem -out ec_secp256k1_private.pem
	rm sec1_ec_secp256k1_private.pem

ec_secp256k1_public.pem: ec_secp256k1_private.pem
	openssl ec -in ec_secp256k1_private.pem -pubout -out ec_secp256k1_public.pem

incorrect_san_correct_subject-cert.pem: incorrect_san_correct_subject-key.pem
	openssl req -x509 \
	            -key incorrect_san_correct_subject-key.pem \
	            -out incorrect_san_correct_subject-cert.pem \
	            -sha256 \
	            -days 3650 \
	            -subj "/CN=good.example.com" \
	            -addext "subjectAltName = DNS:evil.example.com"

incorrect_san_correct_subject-key.pem:
	openssl ecparam -name prime256v1 -genkey -noout -out incorrect_san_correct_subject-key.pem

irrelevant_san_correct_subject-cert.pem: irrelevant_san_correct_subject-key.pem
	openssl req -x509 \
	            -key irrelevant_san_correct_subject-key.pem \
	            -out irrelevant_san_correct_subject-cert.pem \
	            -sha256 \
	            -days 3650 \
	            -subj "/CN=good.example.com" \
	            -addext "subjectAltName = IP:1.2.3.4"

irrelevant_san_correct_subject-key.pem:
	openssl ecparam -name prime256v1 -genkey -noout -out irrelevant_san_correct_subject-key.pem

clean:
	rm -f *.pfx *.pem *.srl ca2-database.txt ca2-serial fake-startcom-root-serial *.print *.old fake-startcom-root-issued-certs/*.pem
	@> fake-startcom-root-database.txt

test: agent1-verify agent2-verify agent3-verify agent4-verify agent5-verify agent6-verify agent7-verify agent8-verify agent10-verify ec10-verify

%-cert.pem.print: %-cert.pem
	openssl x509 -in $< -text -noout > $@

.PHONY: all clean test agent1-verify agent2-verify agent3-verify agent4-verify agent5-verify agent6-verify agent7-verify agent8-verify agent10-verify ec10-verify
