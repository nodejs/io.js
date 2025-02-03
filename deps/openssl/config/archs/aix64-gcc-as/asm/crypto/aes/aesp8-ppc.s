.machine	"any"

.csect	.text[PR],7

.align	7
rcon:
.byte	0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x00,0x00,0x00
.byte	0x1b,0x00,0x00,0x00,0x1b,0x00,0x00,0x00,0x1b,0x00,0x00,0x00,0x1b,0x00,0x00,0x00
.byte	0x0d,0x0e,0x0f,0x0c,0x0d,0x0e,0x0f,0x0c,0x0d,0x0e,0x0f,0x0c,0x0d,0x0e,0x0f,0x0c
.byte	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
.long	0x0f102132, 0x43546576, 0x8798a9ba, 0xcbdcedfe
Lconsts:
	mflr	0
	bcl	20,31,$+4
	mflr	6
	addi	6,6,-0x58
	mtlr	0
	blr	
.long	0
.byte	0,12,0x14,0,0,0,0,0
.byte	65,69,83,32,102,111,114,32,80,111,119,101,114,73,83,65,32,50,46,48,55,44,67,82,89,80,84,79,71,65,77,83,32,98,121,32,60,97,112,112,114,111,64,111,112,101,110,115,115,108,46,111,114,103,62,0
.align	2

.globl	.aes_p8_set_encrypt_key
.align	5
.aes_p8_set_encrypt_key:
Lset_encrypt_key:
	mflr	11
	std	11,16(1)

	li	6,-1
	cmpldi	3,0
	beq-	Lenc_key_abort
	cmpldi	5,0
	beq-	Lenc_key_abort
	li	6,-2
	cmpwi	4,128
	blt-	Lenc_key_abort
	cmpwi	4,256
	bgt-	Lenc_key_abort
	andi.	0,4,0x3f
	bne-	Lenc_key_abort

	lis	0,0xfff0
	li	12,-1
	or	0,0,0

	bl	Lconsts
	mtlr	11

	neg	9,3
	lvx	1,0,3
	addi	3,3,15
	lvsr	3,0,9
	li	8,0x20
	cmpwi	4,192
	lvx	2,0,3

	lvx	4,0,6

	lvx	5,8,6
	addi	6,6,0x10
	vperm	1,1,2,3
	li	7,8
	vxor	0,0,0
	mtctr	7

	lvsr	8,0,5
	vspltisb	9,-1
	lvx	10,0,5
	vperm	9,0,9,8

	blt	Loop128
	addi	3,3,8
	beq	L192
	addi	3,3,8
	b	L256

.align	4
Loop128:
	vperm	3,1,1,5
	vsldoi	6,0,1,12
	vperm	11,1,1,8
	vsel	7,10,11,9
	vor	10,11,11
	.long	0x10632509
	stvx	7,0,5
	addi	5,5,16

	vxor	1,1,6
	vsldoi	6,0,6,12
	vxor	1,1,6
	vsldoi	6,0,6,12
	vxor	1,1,6
	vadduwm	4,4,4
	vxor	1,1,3
	bc	16,0,Loop128

	lvx	4,0,6

	vperm	3,1,1,5
	vsldoi	6,0,1,12
	vperm	11,1,1,8
	vsel	7,10,11,9
	vor	10,11,11
	.long	0x10632509
	stvx	7,0,5
	addi	5,5,16

	vxor	1,1,6
	vsldoi	6,0,6,12
	vxor	1,1,6
	vsldoi	6,0,6,12
	vxor	1,1,6
	vadduwm	4,4,4
	vxor	1,1,3

	vperm	3,1,1,5
	vsldoi	6,0,1,12
	vperm	11,1,1,8
	vsel	7,10,11,9
	vor	10,11,11
	.long	0x10632509
	stvx	7,0,5
	addi	5,5,16

	vxor	1,1,6
	vsldoi	6,0,6,12
	vxor	1,1,6
	vsldoi	6,0,6,12
	vxor	1,1,6
	vxor	1,1,3
	vperm	11,1,1,8
	vsel	7,10,11,9
	vor	10,11,11
	stvx	7,0,5

	addi	3,5,15
	addi	5,5,0x50

	li	8,10
	b	Ldone

.align	4
L192:
	lvx	6,0,3
	li	7,4
	vperm	11,1,1,8
	vsel	7,10,11,9
	vor	10,11,11
	stvx	7,0,5
	addi	5,5,16
	vperm	2,2,6,3
	vspltisb	3,8
	mtctr	7
	vsububm	5,5,3

Loop192:
	vperm	3,2,2,5
	vsldoi	6,0,1,12
	.long	0x10632509

	vxor	1,1,6
	vsldoi	6,0,6,12
	vxor	1,1,6
	vsldoi	6,0,6,12
	vxor	1,1,6

	vsldoi	7,0,2,8
	vspltw	6,1,3
	vxor	6,6,2
	vsldoi	2,0,2,12
	vadduwm	4,4,4
	vxor	2,2,6
	vxor	1,1,3
	vxor	2,2,3
	vsldoi	7,7,1,8

	vperm	3,2,2,5
	vsldoi	6,0,1,12
	vperm	11,7,7,8
	vsel	7,10,11,9
	vor	10,11,11
	.long	0x10632509
	stvx	7,0,5
	addi	5,5,16

	vsldoi	7,1,2,8
	vxor	1,1,6
	vsldoi	6,0,6,12
	vperm	11,7,7,8
	vsel	7,10,11,9
	vor	10,11,11
	vxor	1,1,6
	vsldoi	6,0,6,12
	vxor	1,1,6
	stvx	7,0,5
	addi	5,5,16

	vspltw	6,1,3
	vxor	6,6,2
	vsldoi	2,0,2,12
	vadduwm	4,4,4
	vxor	2,2,6
	vxor	1,1,3
	vxor	2,2,3
	vperm	11,1,1,8
	vsel	7,10,11,9
	vor	10,11,11
	stvx	7,0,5
	addi	3,5,15
	addi	5,5,16
	bc	16,0,Loop192

	li	8,12
	addi	5,5,0x20
	b	Ldone

.align	4
L256:
	lvx	6,0,3
	li	7,7
	li	8,14
	vperm	11,1,1,8
	vsel	7,10,11,9
	vor	10,11,11
	stvx	7,0,5
	addi	5,5,16
	vperm	2,2,6,3
	mtctr	7

Loop256:
	vperm	3,2,2,5
	vsldoi	6,0,1,12
	vperm	11,2,2,8
	vsel	7,10,11,9
	vor	10,11,11
	.long	0x10632509
	stvx	7,0,5
	addi	5,5,16

	vxor	1,1,6
	vsldoi	6,0,6,12
	vxor	1,1,6
	vsldoi	6,0,6,12
	vxor	1,1,6
	vadduwm	4,4,4
	vxor	1,1,3
	vperm	11,1,1,8
	vsel	7,10,11,9
	vor	10,11,11
	stvx	7,0,5
	addi	3,5,15
	addi	5,5,16
	bdz	Ldone

	vspltw	3,1,3
	vsldoi	6,0,2,12
	.long	0x106305C8

	vxor	2,2,6
	vsldoi	6,0,6,12
	vxor	2,2,6
	vsldoi	6,0,6,12
	vxor	2,2,6

	vxor	2,2,3
	b	Loop256

.align	4
Ldone:
	lvx	2,0,3
	vsel	2,10,2,9
	stvx	2,0,3
	li	6,0
	or	12,12,12
	stw	8,0(5)

Lenc_key_abort:
	mr	3,6
	blr	
.long	0
.byte	0,12,0x14,1,0,0,3,0
.long	0


.globl	.aes_p8_set_decrypt_key
.align	5
.aes_p8_set_decrypt_key:
	stdu	1,-64(1)
	mflr	10
	std	10,64+16(1)
	bl	Lset_encrypt_key
	mtlr	10

	cmpwi	3,0
	bne-	Ldec_key_abort

	slwi	7,8,4
	subi	3,5,240
	srwi	8,8,1
	add	5,3,7
	mtctr	8

Ldeckey:
	lwz	0, 0(3)
	lwz	6, 4(3)
	lwz	7, 8(3)
	lwz	8, 12(3)
	addi	3,3,16
	lwz	9, 0(5)
	lwz	10,4(5)
	lwz	11,8(5)
	lwz	12,12(5)
	stw	0, 0(5)
	stw	6, 4(5)
	stw	7, 8(5)
	stw	8, 12(5)
	subi	5,5,16
	stw	9, -16(3)
	stw	10,-12(3)
	stw	11,-8(3)
	stw	12,-4(3)
	bc	16,0,Ldeckey

	xor	3,3,3
Ldec_key_abort:
	addi	1,1,64
	blr	
.long	0
.byte	0,12,4,1,0x80,0,3,0
.long	0

.globl	.aes_p8_encrypt
.align	5
.aes_p8_encrypt:
	lwz	6,240(5)
	lis	0,0xfc00
	li	12,-1
	li	7,15
	or	0,0,0

	lvx	0,0,3
	neg	11,4
	lvx	1,7,3
	lvsl	2,0,3

	lvsl	3,0,11

	li	7,16
	vperm	0,0,1,2
	lvx	1,0,5
	lvsl	5,0,5
	srwi	6,6,1
	lvx	2,7,5
	addi	7,7,16
	subi	6,6,1
	vperm	1,1,2,5

	vxor	0,0,1
	lvx	1,7,5
	addi	7,7,16
	mtctr	6

Loop_enc:
	vperm	2,2,1,5
	.long	0x10001508
	lvx	2,7,5
	addi	7,7,16
	vperm	1,1,2,5
	.long	0x10000D08
	lvx	1,7,5
	addi	7,7,16
	bc	16,0,Loop_enc

	vperm	2,2,1,5
	.long	0x10001508
	lvx	2,7,5
	vperm	1,1,2,5
	.long	0x10000D09

	vspltisb	2,-1
	vxor	1,1,1
	li	7,15
	vperm	2,1,2,3

	lvx	1,0,4
	vperm	0,0,0,3
	vsel	1,1,0,2
	lvx	4,7,4
	stvx	1,0,4
	vsel	0,0,4,2
	stvx	0,7,4

	or	12,12,12
	blr	
.long	0
.byte	0,12,0x14,0,0,0,3,0
.long	0

.globl	.aes_p8_decrypt
.align	5
.aes_p8_decrypt:
	lwz	6,240(5)
	lis	0,0xfc00
	li	12,-1
	li	7,15
	or	0,0,0

	lvx	0,0,3
	neg	11,4
	lvx	1,7,3
	lvsl	2,0,3

	lvsl	3,0,11

	li	7,16
	vperm	0,0,1,2
	lvx	1,0,5
	lvsl	5,0,5
	srwi	6,6,1
	lvx	2,7,5
	addi	7,7,16
	subi	6,6,1
	vperm	1,1,2,5

	vxor	0,0,1
	lvx	1,7,5
	addi	7,7,16
	mtctr	6

Loop_dec:
	vperm	2,2,1,5
	.long	0x10001548
	lvx	2,7,5
	addi	7,7,16
	vperm	1,1,2,5
	.long	0x10000D48
	lvx	1,7,5
	addi	7,7,16
	bc	16,0,Loop_dec

	vperm	2,2,1,5
	.long	0x10001548
	lvx	2,7,5
	vperm	1,1,2,5
	.long	0x10000D49

	vspltisb	2,-1
	vxor	1,1,1
	li	7,15
	vperm	2,1,2,3

	lvx	1,0,4
	vperm	0,0,0,3
	vsel	1,1,0,2
	lvx	4,7,4
	stvx	1,0,4
	vsel	0,0,4,2
	stvx	0,7,4

	or	12,12,12
	blr	
.long	0
.byte	0,12,0x14,0,0,0,3,0
.long	0

.globl	.aes_p8_cbc_encrypt
.align	5
.aes_p8_cbc_encrypt:
	cmpldi	5,16
	bclr	14,0

	cmpwi	8,0
	lis	0,0xffe0
	li	12,-1
	or	0,0,0

	li	10,15
	vxor	0,0,0


	lvx	4,0,7
	lvsl	6,0,7
	lvx	5,10,7

	vperm	4,4,5,6

	neg	11,3
	lvsl	10,0,6
	lwz	9,240(6)

	lvsr	6,0,11
	lvx	5,0,3
	addi	3,3,15


	lvsr	8,0,4
	vspltisb	9,-1
	lvx	7,0,4
	vperm	9,0,9,8


	srwi	9,9,1
	li	10,16
	subi	9,9,1
	beq	Lcbc_dec

Lcbc_enc:
	vor	2,5,5
	lvx	5,0,3
	addi	3,3,16
	mtctr	9
	subi	5,5,16

	lvx	0,0,6
	vperm	2,2,5,6
	lvx	1,10,6
	addi	10,10,16
	vperm	0,0,1,10
	vxor	2,2,0
	lvx	0,10,6
	addi	10,10,16
	vxor	2,2,4

Loop_cbc_enc:
	vperm	1,1,0,10
	.long	0x10420D08
	lvx	1,10,6
	addi	10,10,16
	vperm	0,0,1,10
	.long	0x10420508
	lvx	0,10,6
	addi	10,10,16
	bc	16,0,Loop_cbc_enc

	vperm	1,1,0,10
	.long	0x10420D08
	lvx	1,10,6
	li	10,16
	vperm	0,0,1,10
	.long	0x10820509
	cmpldi	5,16

	vperm	3,4,4,8
	vsel	2,7,3,9
	vor	7,3,3
	stvx	2,0,4
	addi	4,4,16
	bge	Lcbc_enc

	b	Lcbc_done

.align	4
Lcbc_dec:
	cmpldi	5,128
	bge	_aesp8_cbc_decrypt8x
	vor	3,5,5
	lvx	5,0,3
	addi	3,3,16
	mtctr	9
	subi	5,5,16

	lvx	0,0,6
	vperm	3,3,5,6
	lvx	1,10,6
	addi	10,10,16
	vperm	0,0,1,10
	vxor	2,3,0
	lvx	0,10,6
	addi	10,10,16

Loop_cbc_dec:
	vperm	1,1,0,10
	.long	0x10420D48
	lvx	1,10,6
	addi	10,10,16
	vperm	0,0,1,10
	.long	0x10420548
	lvx	0,10,6
	addi	10,10,16
	bc	16,0,Loop_cbc_dec

	vperm	1,1,0,10
	.long	0x10420D48
	lvx	1,10,6
	li	10,16
	vperm	0,0,1,10
	.long	0x10420549
	cmpldi	5,16

	vxor	2,2,4
	vor	4,3,3
	vperm	3,2,2,8
	vsel	2,7,3,9
	vor	7,3,3
	stvx	2,0,4
	addi	4,4,16
	bge	Lcbc_dec

Lcbc_done:
	addi	4,4,-1
	lvx	2,0,4
	vsel	2,7,2,9
	stvx	2,0,4

	neg	8,7
	li	10,15
	vxor	0,0,0
	vspltisb	9,-1

	lvsl	8,0,8
	vperm	9,0,9,8

	lvx	7,0,7
	vperm	4,4,4,8
	vsel	2,7,4,9
	lvx	5,10,7
	stvx	2,0,7
	vsel	2,4,5,9
	stvx	2,10,7

	or	12,12,12
	blr	
.long	0
.byte	0,12,0x14,0,0,0,6,0
.long	0
.align	5
_aesp8_cbc_decrypt8x:
	stdu	1,-448(1)
	li	10,207
	li	11,223
	stvx	20,10,1
	addi	10,10,32
	stvx	21,11,1
	addi	11,11,32
	stvx	22,10,1
	addi	10,10,32
	stvx	23,11,1
	addi	11,11,32
	stvx	24,10,1
	addi	10,10,32
	stvx	25,11,1
	addi	11,11,32
	stvx	26,10,1
	addi	10,10,32
	stvx	27,11,1
	addi	11,11,32
	stvx	28,10,1
	addi	10,10,32
	stvx	29,11,1
	addi	11,11,32
	stvx	30,10,1
	stvx	31,11,1
	li	0,-1
	stw	12,396(1)
	li	8,0x10
	std	26,400(1)
	li	26,0x20
	std	27,408(1)
	li	27,0x30
	std	28,416(1)
	li	28,0x40
	std	29,424(1)
	li	29,0x50
	std	30,432(1)
	li	30,0x60
	std	31,440(1)
	li	31,0x70
	or	0,0,0

	subi	9,9,3
	subi	5,5,128

	lvx	23,0,6
	lvx	30,8,6
	addi	6,6,0x20
	lvx	31,0,6
	vperm	23,23,30,10
	addi	11,1,64+15
	mtctr	9

Load_cbc_dec_key:
	vperm	24,30,31,10
	lvx	30,8,6
	addi	6,6,0x20
	stvx	24,0,11
	vperm	25,31,30,10
	lvx	31,0,6
	stvx	25,8,11
	addi	11,11,0x20
	bc	16,0,Load_cbc_dec_key

	lvx	26,8,6
	vperm	24,30,31,10
	lvx	27,26,6
	stvx	24,0,11
	vperm	25,31,26,10
	lvx	28,27,6
	stvx	25,8,11
	addi	11,1,64+15
	vperm	26,26,27,10
	lvx	29,28,6
	vperm	27,27,28,10
	lvx	30,29,6
	vperm	28,28,29,10
	lvx	31,30,6
	vperm	29,29,30,10
	lvx	14,31,6
	vperm	30,30,31,10
	lvx	24,0,11
	vperm	31,31,14,10
	lvx	25,8,11



	subi	3,3,15


	.long	0x7C001E99


	.long	0x7C281E99

	.long	0x7C5A1E99

	.long	0x7C7B1E99

	.long	0x7D5C1E99

	vxor	14,0,23
	.long	0x7D7D1E99

	vxor	15,1,23
	.long	0x7D9E1E99

	vxor	16,2,23
	.long	0x7DBF1E99
	addi	3,3,0x80

	vxor	17,3,23

	vxor	18,10,23

	vxor	19,11,23
	vxor	20,12,23
	vxor	21,13,23

	mtctr	9
	b	Loop_cbc_dec8x
.align	5
Loop_cbc_dec8x:
	.long	0x11CEC548
	.long	0x11EFC548
	.long	0x1210C548
	.long	0x1231C548
	.long	0x1252C548
	.long	0x1273C548
	.long	0x1294C548
	.long	0x12B5C548
	lvx	24,26,11
	addi	11,11,0x20

	.long	0x11CECD48
	.long	0x11EFCD48
	.long	0x1210CD48
	.long	0x1231CD48
	.long	0x1252CD48
	.long	0x1273CD48
	.long	0x1294CD48
	.long	0x12B5CD48
	lvx	25,8,11
	bc	16,0,Loop_cbc_dec8x

	subic	5,5,128
	.long	0x11CEC548
	.long	0x11EFC548
	.long	0x1210C548
	.long	0x1231C548
	.long	0x1252C548
	.long	0x1273C548
	.long	0x1294C548
	.long	0x12B5C548

	subfe.	0,0,0
	.long	0x11CECD48
	.long	0x11EFCD48
	.long	0x1210CD48
	.long	0x1231CD48
	.long	0x1252CD48
	.long	0x1273CD48
	.long	0x1294CD48
	.long	0x12B5CD48

	and	0,0,5
	.long	0x11CED548
	.long	0x11EFD548
	.long	0x1210D548
	.long	0x1231D548
	.long	0x1252D548
	.long	0x1273D548
	.long	0x1294D548
	.long	0x12B5D548

	add	3,3,0



	.long	0x11CEDD48
	.long	0x11EFDD48
	.long	0x1210DD48
	.long	0x1231DD48
	.long	0x1252DD48
	.long	0x1273DD48
	.long	0x1294DD48
	.long	0x12B5DD48

	addi	11,1,64+15
	.long	0x11CEE548
	.long	0x11EFE548
	.long	0x1210E548
	.long	0x1231E548
	.long	0x1252E548
	.long	0x1273E548
	.long	0x1294E548
	.long	0x12B5E548
	lvx	24,0,11

	.long	0x11CEED48
	.long	0x11EFED48
	.long	0x1210ED48
	.long	0x1231ED48
	.long	0x1252ED48
	.long	0x1273ED48
	.long	0x1294ED48
	.long	0x12B5ED48
	lvx	25,8,11

	.long	0x11CEF548
	vxor	4,4,31
	.long	0x11EFF548
	vxor	0,0,31
	.long	0x1210F548
	vxor	1,1,31
	.long	0x1231F548
	vxor	2,2,31
	.long	0x1252F548
	vxor	3,3,31
	.long	0x1273F548
	vxor	10,10,31
	.long	0x1294F548
	vxor	11,11,31
	.long	0x12B5F548
	vxor	12,12,31

	.long	0x11CE2549
	.long	0x11EF0549
	.long	0x7C001E99
	.long	0x12100D49
	.long	0x7C281E99
	.long	0x12311549

	.long	0x7C5A1E99
	.long	0x12521D49

	.long	0x7C7B1E99
	.long	0x12735549

	.long	0x7D5C1E99
	.long	0x12945D49

	.long	0x7D7D1E99
	.long	0x12B56549

	.long	0x7D9E1E99
	vor	4,13,13

	.long	0x7DBF1E99
	addi	3,3,0x80



	.long	0x7DC02799

	vxor	14,0,23

	.long	0x7DE82799

	vxor	15,1,23

	.long	0x7E1A2799
	vxor	16,2,23

	.long	0x7E3B2799
	vxor	17,3,23

	.long	0x7E5C2799
	vxor	18,10,23

	.long	0x7E7D2799
	vxor	19,11,23

	.long	0x7E9E2799
	vxor	20,12,23
	.long	0x7EBF2799
	addi	4,4,0x80
	vxor	21,13,23

	mtctr	9
	beq	Loop_cbc_dec8x

	addic.	5,5,128
	beq	Lcbc_dec8x_done
	nop	
	nop	

Loop_cbc_dec8x_tail:
	.long	0x11EFC548
	.long	0x1210C548
	.long	0x1231C548
	.long	0x1252C548
	.long	0x1273C548
	.long	0x1294C548
	.long	0x12B5C548
	lvx	24,26,11
	addi	11,11,0x20

	.long	0x11EFCD48
	.long	0x1210CD48
	.long	0x1231CD48
	.long	0x1252CD48
	.long	0x1273CD48
	.long	0x1294CD48
	.long	0x12B5CD48
	lvx	25,8,11
	bc	16,0,Loop_cbc_dec8x_tail

	.long	0x11EFC548
	.long	0x1210C548
	.long	0x1231C548
	.long	0x1252C548
	.long	0x1273C548
	.long	0x1294C548
	.long	0x12B5C548

	.long	0x11EFCD48
	.long	0x1210CD48
	.long	0x1231CD48
	.long	0x1252CD48
	.long	0x1273CD48
	.long	0x1294CD48
	.long	0x12B5CD48

	.long	0x11EFD548
	.long	0x1210D548
	.long	0x1231D548
	.long	0x1252D548
	.long	0x1273D548
	.long	0x1294D548
	.long	0x12B5D548

	.long	0x11EFDD48
	.long	0x1210DD48
	.long	0x1231DD48
	.long	0x1252DD48
	.long	0x1273DD48
	.long	0x1294DD48
	.long	0x12B5DD48

	.long	0x11EFE548
	.long	0x1210E548
	.long	0x1231E548
	.long	0x1252E548
	.long	0x1273E548
	.long	0x1294E548
	.long	0x12B5E548

	.long	0x11EFED48
	.long	0x1210ED48
	.long	0x1231ED48
	.long	0x1252ED48
	.long	0x1273ED48
	.long	0x1294ED48
	.long	0x12B5ED48

	.long	0x11EFF548
	vxor	4,4,31
	.long	0x1210F548
	vxor	1,1,31
	.long	0x1231F548
	vxor	2,2,31
	.long	0x1252F548
	vxor	3,3,31
	.long	0x1273F548
	vxor	10,10,31
	.long	0x1294F548
	vxor	11,11,31
	.long	0x12B5F548
	vxor	12,12,31

	cmplwi	5,32
	blt	Lcbc_dec8x_one
	nop	
	beq	Lcbc_dec8x_two
	cmplwi	5,64
	blt	Lcbc_dec8x_three
	nop	
	beq	Lcbc_dec8x_four
	cmplwi	5,96
	blt	Lcbc_dec8x_five
	nop	
	beq	Lcbc_dec8x_six

Lcbc_dec8x_seven:
	.long	0x11EF2549
	.long	0x12100D49
	.long	0x12311549
	.long	0x12521D49
	.long	0x12735549
	.long	0x12945D49
	.long	0x12B56549
	vor	4,13,13



	.long	0x7DE02799

	.long	0x7E082799

	.long	0x7E3A2799

	.long	0x7E5B2799

	.long	0x7E7C2799

	.long	0x7E9D2799
	.long	0x7EBE2799
	addi	4,4,0x70
	b	Lcbc_dec8x_done

.align	5
Lcbc_dec8x_six:
	.long	0x12102549
	.long	0x12311549
	.long	0x12521D49
	.long	0x12735549
	.long	0x12945D49
	.long	0x12B56549
	vor	4,13,13



	.long	0x7E002799

	.long	0x7E282799

	.long	0x7E5A2799

	.long	0x7E7B2799

	.long	0x7E9C2799
	.long	0x7EBD2799
	addi	4,4,0x60
	b	Lcbc_dec8x_done

.align	5
Lcbc_dec8x_five:
	.long	0x12312549
	.long	0x12521D49
	.long	0x12735549
	.long	0x12945D49
	.long	0x12B56549
	vor	4,13,13



	.long	0x7E202799

	.long	0x7E482799

	.long	0x7E7A2799

	.long	0x7E9B2799
	.long	0x7EBC2799
	addi	4,4,0x50
	b	Lcbc_dec8x_done

.align	5
Lcbc_dec8x_four:
	.long	0x12522549
	.long	0x12735549
	.long	0x12945D49
	.long	0x12B56549
	vor	4,13,13



	.long	0x7E402799

	.long	0x7E682799

	.long	0x7E9A2799
	.long	0x7EBB2799
	addi	4,4,0x40
	b	Lcbc_dec8x_done

.align	5
Lcbc_dec8x_three:
	.long	0x12732549
	.long	0x12945D49
	.long	0x12B56549
	vor	4,13,13



	.long	0x7E602799

	.long	0x7E882799
	.long	0x7EBA2799
	addi	4,4,0x30
	b	Lcbc_dec8x_done

.align	5
Lcbc_dec8x_two:
	.long	0x12942549
	.long	0x12B56549
	vor	4,13,13



	.long	0x7E802799
	.long	0x7EA82799
	addi	4,4,0x20
	b	Lcbc_dec8x_done

.align	5
Lcbc_dec8x_one:
	.long	0x12B52549
	vor	4,13,13


	.long	0x7EA02799
	addi	4,4,0x10

Lcbc_dec8x_done:

	.long	0x7C803F99

	li	10,79
	li	11,95
	stvx	6,10,1
	addi	10,10,32
	stvx	6,11,1
	addi	11,11,32
	stvx	6,10,1
	addi	10,10,32
	stvx	6,11,1
	addi	11,11,32
	stvx	6,10,1
	addi	10,10,32
	stvx	6,11,1
	addi	11,11,32
	stvx	6,10,1
	addi	10,10,32
	stvx	6,11,1
	addi	11,11,32

	or	12,12,12
	lvx	20,10,1
	addi	10,10,32
	lvx	21,11,1
	addi	11,11,32
	lvx	22,10,1
	addi	10,10,32
	lvx	23,11,1
	addi	11,11,32
	lvx	24,10,1
	addi	10,10,32
	lvx	25,11,1
	addi	11,11,32
	lvx	26,10,1
	addi	10,10,32
	lvx	27,11,1
	addi	11,11,32
	lvx	28,10,1
	addi	10,10,32
	lvx	29,11,1
	addi	11,11,32
	lvx	30,10,1
	lvx	31,11,1
	ld	26,400(1)
	ld	27,408(1)
	ld	28,416(1)
	ld	29,424(1)
	ld	30,432(1)
	ld	31,440(1)
	addi	1,1,448
	blr	
.long	0
.byte	0,12,0x04,0,0x80,6,6,0
.long	0

.globl	.aes_p8_ctr32_encrypt_blocks
.align	5
.aes_p8_ctr32_encrypt_blocks:
	cmpldi	5,1
	bclr	14,0

	lis	0,0xfff0
	li	12,-1
	or	0,0,0

	li	10,15
	vxor	0,0,0


	lvx	4,0,7
	lvsl	6,0,7
	lvx	5,10,7
	vspltisb	11,1

	vperm	4,4,5,6
	vsldoi	11,0,11,1

	neg	11,3
	lvsl	10,0,6
	lwz	9,240(6)

	lvsr	6,0,11
	lvx	5,0,3
	addi	3,3,15


	srwi	9,9,1
	li	10,16
	subi	9,9,1

	cmpldi	5,8
	bge	_aesp8_ctr32_encrypt8x

	lvsr	8,0,4
	vspltisb	9,-1
	lvx	7,0,4
	vperm	9,0,9,8


	lvx	0,0,6
	mtctr	9
	lvx	1,10,6
	addi	10,10,16
	vperm	0,0,1,10
	vxor	2,4,0
	lvx	0,10,6
	addi	10,10,16
	b	Loop_ctr32_enc

.align	5
Loop_ctr32_enc:
	vperm	1,1,0,10
	.long	0x10420D08
	lvx	1,10,6
	addi	10,10,16
	vperm	0,0,1,10
	.long	0x10420508
	lvx	0,10,6
	addi	10,10,16
	bc	16,0,Loop_ctr32_enc

	vadduwm	4,4,11
	vor	3,5,5
	lvx	5,0,3
	addi	3,3,16
	subic.	5,5,1

	vperm	1,1,0,10
	.long	0x10420D08
	lvx	1,10,6
	vperm	3,3,5,6
	li	10,16
	vperm	1,0,1,10
	lvx	0,0,6
	vxor	3,3,1
	.long	0x10421D09

	lvx	1,10,6
	addi	10,10,16
	vperm	2,2,2,8
	vsel	3,7,2,9
	mtctr	9
	vperm	0,0,1,10
	vor	7,2,2
	vxor	2,4,0
	lvx	0,10,6
	addi	10,10,16
	stvx	3,0,4
	addi	4,4,16
	bne	Loop_ctr32_enc

	addi	4,4,-1
	lvx	2,0,4
	vsel	2,7,2,9
	stvx	2,0,4

	or	12,12,12
	blr	
.long	0
.byte	0,12,0x14,0,0,0,6,0
.long	0
.align	5
_aesp8_ctr32_encrypt8x:
	stdu	1,-448(1)
	li	10,207
	li	11,223
	stvx	20,10,1
	addi	10,10,32
	stvx	21,11,1
	addi	11,11,32
	stvx	22,10,1
	addi	10,10,32
	stvx	23,11,1
	addi	11,11,32
	stvx	24,10,1
	addi	10,10,32
	stvx	25,11,1
	addi	11,11,32
	stvx	26,10,1
	addi	10,10,32
	stvx	27,11,1
	addi	11,11,32
	stvx	28,10,1
	addi	10,10,32
	stvx	29,11,1
	addi	11,11,32
	stvx	30,10,1
	stvx	31,11,1
	li	0,-1
	stw	12,396(1)
	li	8,0x10
	std	26,400(1)
	li	26,0x20
	std	27,408(1)
	li	27,0x30
	std	28,416(1)
	li	28,0x40
	std	29,424(1)
	li	29,0x50
	std	30,432(1)
	li	30,0x60
	std	31,440(1)
	li	31,0x70
	or	0,0,0

	subi	9,9,3

	lvx	23,0,6
	lvx	30,8,6
	addi	6,6,0x20
	lvx	31,0,6
	vperm	23,23,30,10
	addi	11,1,64+15
	mtctr	9

Load_ctr32_enc_key:
	vperm	24,30,31,10
	lvx	30,8,6
	addi	6,6,0x20
	stvx	24,0,11
	vperm	25,31,30,10
	lvx	31,0,6
	stvx	25,8,11
	addi	11,11,0x20
	bc	16,0,Load_ctr32_enc_key

	lvx	26,8,6
	vperm	24,30,31,10
	lvx	27,26,6
	stvx	24,0,11
	vperm	25,31,26,10
	lvx	28,27,6
	stvx	25,8,11
	addi	11,1,64+15
	vperm	26,26,27,10
	lvx	29,28,6
	vperm	27,27,28,10
	lvx	30,29,6
	vperm	28,28,29,10
	lvx	31,30,6
	vperm	29,29,30,10
	lvx	15,31,6
	vperm	30,30,31,10
	lvx	24,0,11
	vperm	31,31,15,10
	lvx	25,8,11

	vadduwm	7,11,11
	subi	3,3,15
	sldi	5,5,4

	vadduwm	16,4,11
	vadduwm	17,4,7
	vxor	15,4,23

	vadduwm	18,16,7
	vxor	16,16,23

	vadduwm	19,17,7
	vxor	17,17,23

	vadduwm	20,18,7
	vxor	18,18,23

	vadduwm	21,19,7
	vxor	19,19,23
	vadduwm	22,20,7
	vxor	20,20,23
	vadduwm	4,21,7
	vxor	21,21,23
	vxor	22,22,23

	mtctr	9
	b	Loop_ctr32_enc8x
.align	5
Loop_ctr32_enc8x:
	.long	0x11EFC508
	.long	0x1210C508
	.long	0x1231C508
	.long	0x1252C508
	.long	0x1273C508
	.long	0x1294C508
	.long	0x12B5C508
	.long	0x12D6C508
Loop_ctr32_enc8x_middle:
	lvx	24,26,11
	addi	11,11,0x20

	.long	0x11EFCD08
	.long	0x1210CD08
	.long	0x1231CD08
	.long	0x1252CD08
	.long	0x1273CD08
	.long	0x1294CD08
	.long	0x12B5CD08
	.long	0x12D6CD08
	lvx	25,8,11
	bc	16,0,Loop_ctr32_enc8x

	subic	11,5,256
	.long	0x11EFC508
	.long	0x1210C508
	.long	0x1231C508
	.long	0x1252C508
	.long	0x1273C508
	.long	0x1294C508
	.long	0x12B5C508
	.long	0x12D6C508

	subfe	0,0,0
	.long	0x11EFCD08
	.long	0x1210CD08
	.long	0x1231CD08
	.long	0x1252CD08
	.long	0x1273CD08
	.long	0x1294CD08
	.long	0x12B5CD08
	.long	0x12D6CD08

	and	0,0,11
	addi	11,1,64+15
	.long	0x11EFD508
	.long	0x1210D508
	.long	0x1231D508
	.long	0x1252D508
	.long	0x1273D508
	.long	0x1294D508
	.long	0x12B5D508
	.long	0x12D6D508
	lvx	24,0,11

	subic	5,5,129
	.long	0x11EFDD08
	addi	5,5,1
	.long	0x1210DD08
	.long	0x1231DD08
	.long	0x1252DD08
	.long	0x1273DD08
	.long	0x1294DD08
	.long	0x12B5DD08
	.long	0x12D6DD08
	lvx	25,8,11

	.long	0x11EFE508
	.long	0x7C001E99
	.long	0x1210E508
	.long	0x7C281E99
	.long	0x1231E508
	.long	0x7C5A1E99
	.long	0x1252E508
	.long	0x7C7B1E99
	.long	0x1273E508
	.long	0x7D5C1E99
	.long	0x1294E508
	.long	0x7D9D1E99
	.long	0x12B5E508
	.long	0x7DBE1E99
	.long	0x12D6E508
	.long	0x7DDF1E99
	addi	3,3,0x80

	.long	0x11EFED08

	.long	0x1210ED08

	.long	0x1231ED08

	.long	0x1252ED08

	.long	0x1273ED08

	.long	0x1294ED08

	.long	0x12B5ED08

	.long	0x12D6ED08


	add	3,3,0



	subfe.	0,0,0
	.long	0x11EFF508
	vxor	0,0,31
	.long	0x1210F508
	vxor	1,1,31
	.long	0x1231F508
	vxor	2,2,31
	.long	0x1252F508
	vxor	3,3,31
	.long	0x1273F508
	vxor	10,10,31
	.long	0x1294F508
	vxor	12,12,31
	.long	0x12B5F508
	vxor	13,13,31
	.long	0x12D6F508
	vxor	14,14,31

	bne	Lctr32_enc8x_break

	.long	0x100F0509
	.long	0x10300D09
	vadduwm	16,4,11
	.long	0x10511509
	vadduwm	17,4,7
	vxor	15,4,23
	.long	0x10721D09
	vadduwm	18,16,7
	vxor	16,16,23
	.long	0x11535509
	vadduwm	19,17,7
	vxor	17,17,23
	.long	0x11946509
	vadduwm	20,18,7
	vxor	18,18,23
	.long	0x11B56D09
	vadduwm	21,19,7
	vxor	19,19,23
	.long	0x11D67509
	vadduwm	22,20,7
	vxor	20,20,23

	vadduwm	4,21,7
	vxor	21,21,23

	vxor	22,22,23
	mtctr	9

	.long	0x11EFC508
	.long	0x7C002799

	.long	0x1210C508
	.long	0x7C282799

	.long	0x1231C508
	.long	0x7C5A2799

	.long	0x1252C508
	.long	0x7C7B2799

	.long	0x1273C508
	.long	0x7D5C2799

	.long	0x1294C508
	.long	0x7D9D2799

	.long	0x12B5C508
	.long	0x7DBE2799
	.long	0x12D6C508
	.long	0x7DDF2799
	addi	4,4,0x80

	b	Loop_ctr32_enc8x_middle

.align	5
Lctr32_enc8x_break:
	cmpwi	5,-0x60
	blt	Lctr32_enc8x_one
	nop	
	beq	Lctr32_enc8x_two
	cmpwi	5,-0x40
	blt	Lctr32_enc8x_three
	nop	
	beq	Lctr32_enc8x_four
	cmpwi	5,-0x20
	blt	Lctr32_enc8x_five
	nop	
	beq	Lctr32_enc8x_six
	cmpwi	5,0x00
	blt	Lctr32_enc8x_seven

Lctr32_enc8x_eight:
	.long	0x11EF0509
	.long	0x12100D09
	.long	0x12311509
	.long	0x12521D09
	.long	0x12735509
	.long	0x12946509
	.long	0x12B56D09
	.long	0x12D67509



	.long	0x7DE02799

	.long	0x7E082799

	.long	0x7E3A2799

	.long	0x7E5B2799

	.long	0x7E7C2799

	.long	0x7E9D2799

	.long	0x7EBE2799
	.long	0x7EDF2799
	addi	4,4,0x80
	b	Lctr32_enc8x_done

.align	5
Lctr32_enc8x_seven:
	.long	0x11EF0D09
	.long	0x12101509
	.long	0x12311D09
	.long	0x12525509
	.long	0x12736509
	.long	0x12946D09
	.long	0x12B57509



	.long	0x7DE02799

	.long	0x7E082799

	.long	0x7E3A2799

	.long	0x7E5B2799

	.long	0x7E7C2799

	.long	0x7E9D2799
	.long	0x7EBE2799
	addi	4,4,0x70
	b	Lctr32_enc8x_done

.align	5
Lctr32_enc8x_six:
	.long	0x11EF1509
	.long	0x12101D09
	.long	0x12315509
	.long	0x12526509
	.long	0x12736D09
	.long	0x12947509



	.long	0x7DE02799

	.long	0x7E082799

	.long	0x7E3A2799

	.long	0x7E5B2799

	.long	0x7E7C2799
	.long	0x7E9D2799
	addi	4,4,0x60
	b	Lctr32_enc8x_done

.align	5
Lctr32_enc8x_five:
	.long	0x11EF1D09
	.long	0x12105509
	.long	0x12316509
	.long	0x12526D09
	.long	0x12737509



	.long	0x7DE02799

	.long	0x7E082799

	.long	0x7E3A2799

	.long	0x7E5B2799
	.long	0x7E7C2799
	addi	4,4,0x50
	b	Lctr32_enc8x_done

.align	5
Lctr32_enc8x_four:
	.long	0x11EF5509
	.long	0x12106509
	.long	0x12316D09
	.long	0x12527509



	.long	0x7DE02799

	.long	0x7E082799

	.long	0x7E3A2799
	.long	0x7E5B2799
	addi	4,4,0x40
	b	Lctr32_enc8x_done

.align	5
Lctr32_enc8x_three:
	.long	0x11EF6509
	.long	0x12106D09
	.long	0x12317509



	.long	0x7DE02799

	.long	0x7E082799
	.long	0x7E3A2799
	addi	4,4,0x30
	b	Lctr32_enc8x_done

.align	5
Lctr32_enc8x_two:
	.long	0x11EF6D09
	.long	0x12107509



	.long	0x7DE02799
	.long	0x7E082799
	addi	4,4,0x20
	b	Lctr32_enc8x_done

.align	5
Lctr32_enc8x_one:
	.long	0x11EF7509


	.long	0x7DE02799
	addi	4,4,0x10

Lctr32_enc8x_done:
	li	10,79
	li	11,95
	stvx	6,10,1
	addi	10,10,32
	stvx	6,11,1
	addi	11,11,32
	stvx	6,10,1
	addi	10,10,32
	stvx	6,11,1
	addi	11,11,32
	stvx	6,10,1
	addi	10,10,32
	stvx	6,11,1
	addi	11,11,32
	stvx	6,10,1
	addi	10,10,32
	stvx	6,11,1
	addi	11,11,32

	or	12,12,12
	lvx	20,10,1
	addi	10,10,32
	lvx	21,11,1
	addi	11,11,32
	lvx	22,10,1
	addi	10,10,32
	lvx	23,11,1
	addi	11,11,32
	lvx	24,10,1
	addi	10,10,32
	lvx	25,11,1
	addi	11,11,32
	lvx	26,10,1
	addi	10,10,32
	lvx	27,11,1
	addi	11,11,32
	lvx	28,10,1
	addi	10,10,32
	lvx	29,11,1
	addi	11,11,32
	lvx	30,10,1
	lvx	31,11,1
	ld	26,400(1)
	ld	27,408(1)
	ld	28,416(1)
	ld	29,424(1)
	ld	30,432(1)
	ld	31,440(1)
	addi	1,1,448
	blr	
.long	0
.byte	0,12,0x04,0,0x80,6,6,0
.long	0

.globl	.aes_p8_xts_encrypt
.align	5
.aes_p8_xts_encrypt:
	mr	10,3
	li	3,-1
	cmpldi	5,16
	bclr	14,0

	lis	0,0xfff0
	li	12,-1
	li	11,0
	or	0,0,0

	vspltisb	9,0x07




	li	3,15
	lvx	8,0,8
	lvsl	5,0,8
	lvx	4,3,8

	vperm	8,8,4,5

	neg	11,10
	lvsr	5,0,11
	lvx	2,0,10
	addi	10,10,15


	cmpldi	7,0
	beq	Lxts_enc_no_key2

	lvsl	7,0,7
	lwz	9,240(7)
	srwi	9,9,1
	subi	9,9,1
	li	3,16

	lvx	0,0,7
	lvx	1,3,7
	addi	3,3,16
	vperm	0,0,1,7
	vxor	8,8,0
	lvx	0,3,7
	addi	3,3,16
	mtctr	9

Ltweak_xts_enc:
	vperm	1,1,0,7
	.long	0x11080D08
	lvx	1,3,7
	addi	3,3,16
	vperm	0,0,1,7
	.long	0x11080508
	lvx	0,3,7
	addi	3,3,16
	bc	16,0,Ltweak_xts_enc

	vperm	1,1,0,7
	.long	0x11080D08
	lvx	1,3,7
	vperm	0,0,1,7
	.long	0x11080509

	li	8,0
	b	Lxts_enc

Lxts_enc_no_key2:
	li	3,-16
	and	5,5,3


Lxts_enc:
	lvx	4,0,10
	addi	10,10,16

	lvsl	7,0,6
	lwz	9,240(6)
	srwi	9,9,1
	subi	9,9,1
	li	3,16

	vslb	10,9,9
	vor	10,10,9
	vspltisb	11,1
	vsldoi	10,10,11,15

	cmpldi	5,96
	bge	_aesp8_xts_encrypt6x

	andi.	7,5,15
	subic	0,5,32
	subi	7,7,16
	subfe	0,0,0
	and	0,0,7
	add	10,10,0

	lvx	0,0,6
	lvx	1,3,6
	addi	3,3,16
	vperm	2,2,4,5
	vperm	0,0,1,7
	vxor	2,2,8
	vxor	2,2,0
	lvx	0,3,6
	addi	3,3,16
	mtctr	9
	b	Loop_xts_enc

.align	5
Loop_xts_enc:
	vperm	1,1,0,7
	.long	0x10420D08
	lvx	1,3,6
	addi	3,3,16
	vperm	0,0,1,7
	.long	0x10420508
	lvx	0,3,6
	addi	3,3,16
	bc	16,0,Loop_xts_enc

	vperm	1,1,0,7
	.long	0x10420D08
	lvx	1,3,6
	li	3,16
	vperm	0,0,1,7
	vxor	0,0,8
	.long	0x10620509


	nop	

	.long	0x7C602799
	addi	4,4,16

	subic.	5,5,16
	beq	Lxts_enc_done

	vor	2,4,4
	lvx	4,0,10
	addi	10,10,16
	lvx	0,0,6
	lvx	1,3,6
	addi	3,3,16

	subic	0,5,32
	subfe	0,0,0
	and	0,0,7
	add	10,10,0

	vsrab	11,8,9
	vaddubm	8,8,8
	vsldoi	11,11,11,15
	vand	11,11,10
	vxor	8,8,11

	vperm	2,2,4,5
	vperm	0,0,1,7
	vxor	2,2,8
	vxor	3,3,0
	vxor	2,2,0
	lvx	0,3,6
	addi	3,3,16

	mtctr	9
	cmpldi	5,16
	bge	Loop_xts_enc

	vxor	3,3,8
	lvsr	5,0,5
	vxor	4,4,4
	vspltisb	11,-1
	vperm	4,4,11,5
	vsel	2,2,3,4

	subi	11,4,17
	subi	4,4,16
	mtctr	5
	li	5,16
Loop_xts_enc_steal:
	lbzu	0,1(11)
	stb	0,16(11)
	bc	16,0,Loop_xts_enc_steal

	mtctr	9
	b	Loop_xts_enc

Lxts_enc_done:
	cmpldi	8,0
	beq	Lxts_enc_ret

	vsrab	11,8,9
	vaddubm	8,8,8
	vsldoi	11,11,11,15
	vand	11,11,10
	vxor	8,8,11


	.long	0x7D004799

Lxts_enc_ret:
	or	12,12,12
	li	3,0
	blr	
.long	0
.byte	0,12,0x04,0,0x80,6,6,0
.long	0


.globl	.aes_p8_xts_decrypt
.align	5
.aes_p8_xts_decrypt:
	mr	10,3
	li	3,-1
	cmpldi	5,16
	bclr	14,0

	lis	0,0xfff8
	li	12,-1
	li	11,0
	or	0,0,0

	andi.	0,5,15
	neg	0,0
	andi.	0,0,16
	sub	5,5,0

	vspltisb	9,0x07




	li	3,15
	lvx	8,0,8
	lvsl	5,0,8
	lvx	4,3,8

	vperm	8,8,4,5

	neg	11,10
	lvsr	5,0,11
	lvx	2,0,10
	addi	10,10,15


	cmpldi	7,0
	beq	Lxts_dec_no_key2

	lvsl	7,0,7
	lwz	9,240(7)
	srwi	9,9,1
	subi	9,9,1
	li	3,16

	lvx	0,0,7
	lvx	1,3,7
	addi	3,3,16
	vperm	0,0,1,7
	vxor	8,8,0
	lvx	0,3,7
	addi	3,3,16
	mtctr	9

Ltweak_xts_dec:
	vperm	1,1,0,7
	.long	0x11080D08
	lvx	1,3,7
	addi	3,3,16
	vperm	0,0,1,7
	.long	0x11080508
	lvx	0,3,7
	addi	3,3,16
	bc	16,0,Ltweak_xts_dec

	vperm	1,1,0,7
	.long	0x11080D08
	lvx	1,3,7
	vperm	0,0,1,7
	.long	0x11080509

	li	8,0
	b	Lxts_dec

Lxts_dec_no_key2:
	neg	3,5
	andi.	3,3,15
	add	5,5,3


Lxts_dec:
	lvx	4,0,10
	addi	10,10,16

	lvsl	7,0,6
	lwz	9,240(6)
	srwi	9,9,1
	subi	9,9,1
	li	3,16

	vslb	10,9,9
	vor	10,10,9
	vspltisb	11,1
	vsldoi	10,10,11,15

	cmpldi	5,96
	bge	_aesp8_xts_decrypt6x

	lvx	0,0,6
	lvx	1,3,6
	addi	3,3,16
	vperm	2,2,4,5
	vperm	0,0,1,7
	vxor	2,2,8
	vxor	2,2,0
	lvx	0,3,6
	addi	3,3,16
	mtctr	9

	cmpldi	5,16
	blt	Ltail_xts_dec
	b	Loop_xts_dec

.align	5
Loop_xts_dec:
	vperm	1,1,0,7
	.long	0x10420D48
	lvx	1,3,6
	addi	3,3,16
	vperm	0,0,1,7
	.long	0x10420548
	lvx	0,3,6
	addi	3,3,16
	bc	16,0,Loop_xts_dec

	vperm	1,1,0,7
	.long	0x10420D48
	lvx	1,3,6
	li	3,16
	vperm	0,0,1,7
	vxor	0,0,8
	.long	0x10620549


	nop	

	.long	0x7C602799
	addi	4,4,16

	subic.	5,5,16
	beq	Lxts_dec_done

	vor	2,4,4
	lvx	4,0,10
	addi	10,10,16
	lvx	0,0,6
	lvx	1,3,6
	addi	3,3,16

	vsrab	11,8,9
	vaddubm	8,8,8
	vsldoi	11,11,11,15
	vand	11,11,10
	vxor	8,8,11

	vperm	2,2,4,5
	vperm	0,0,1,7
	vxor	2,2,8
	vxor	2,2,0
	lvx	0,3,6
	addi	3,3,16

	mtctr	9
	cmpldi	5,16
	bge	Loop_xts_dec

Ltail_xts_dec:
	vsrab	11,8,9
	vaddubm	12,8,8
	vsldoi	11,11,11,15
	vand	11,11,10
	vxor	12,12,11

	subi	10,10,16
	add	10,10,5

	vxor	2,2,8
	vxor	2,2,12

Loop_xts_dec_short:
	vperm	1,1,0,7
	.long	0x10420D48
	lvx	1,3,6
	addi	3,3,16
	vperm	0,0,1,7
	.long	0x10420548
	lvx	0,3,6
	addi	3,3,16
	bc	16,0,Loop_xts_dec_short

	vperm	1,1,0,7
	.long	0x10420D48
	lvx	1,3,6
	li	3,16
	vperm	0,0,1,7
	vxor	0,0,12
	.long	0x10620549


	nop	

	.long	0x7C602799

	vor	2,4,4
	lvx	4,0,10

	lvx	0,0,6
	lvx	1,3,6
	addi	3,3,16
	vperm	2,2,4,5
	vperm	0,0,1,7

	lvsr	5,0,5
	vxor	4,4,4
	vspltisb	11,-1
	vperm	4,4,11,5
	vsel	2,2,3,4

	vxor	0,0,8
	vxor	2,2,0
	lvx	0,3,6
	addi	3,3,16

	subi	11,4,1
	mtctr	5
	li	5,16
Loop_xts_dec_steal:
	lbzu	0,1(11)
	stb	0,16(11)
	bc	16,0,Loop_xts_dec_steal

	mtctr	9
	b	Loop_xts_dec

Lxts_dec_done:
	cmpldi	8,0
	beq	Lxts_dec_ret

	vsrab	11,8,9
	vaddubm	8,8,8
	vsldoi	11,11,11,15
	vand	11,11,10
	vxor	8,8,11


	.long	0x7D004799

Lxts_dec_ret:
	or	12,12,12
	li	3,0
	blr	
.long	0
.byte	0,12,0x04,0,0x80,6,6,0
.long	0

.align	5
_aesp8_xts_encrypt6x:
	stdu	1,-448(1)
	mflr	11
	li	7,207
	li	3,223
	std	11,464(1)
	stvx	20,7,1
	addi	7,7,32
	stvx	21,3,1
	addi	3,3,32
	stvx	22,7,1
	addi	7,7,32
	stvx	23,3,1
	addi	3,3,32
	stvx	24,7,1
	addi	7,7,32
	stvx	25,3,1
	addi	3,3,32
	stvx	26,7,1
	addi	7,7,32
	stvx	27,3,1
	addi	3,3,32
	stvx	28,7,1
	addi	7,7,32
	stvx	29,3,1
	addi	3,3,32
	stvx	30,7,1
	stvx	31,3,1
	li	0,-1
	stw	12,396(1)
	li	3,0x10
	std	26,400(1)
	li	26,0x20
	std	27,408(1)
	li	27,0x30
	std	28,416(1)
	li	28,0x40
	std	29,424(1)
	li	29,0x50
	std	30,432(1)
	li	30,0x60
	std	31,440(1)
	li	31,0x70
	or	0,0,0


	xxlor	2, 32+10, 32+10
	vsldoi	10,11,10,1
	xxlor	1, 32+10, 32+10


	mr	31, 6
	bl	Lconsts
	lxvw4x	0, 28, 6
	mr	6, 31
	li	31,0x70

	subi	9,9,3

	lvx	23,0,6
	lvx	30,3,6
	addi	6,6,0x20
	lvx	31,0,6
	vperm	23,23,30,7
	addi	7,1,64+15
	mtctr	9

Load_xts_enc_key:
	vperm	24,30,31,7
	lvx	30,3,6
	addi	6,6,0x20
	stvx	24,0,7
	vperm	25,31,30,7
	lvx	31,0,6
	stvx	25,3,7
	addi	7,7,0x20
	bc	16,0,Load_xts_enc_key

	lvx	26,3,6
	vperm	24,30,31,7
	lvx	27,26,6
	stvx	24,0,7
	vperm	25,31,26,7
	lvx	28,27,6
	stvx	25,3,7
	addi	7,1,64+15
	vperm	26,26,27,7
	lvx	29,28,6
	vperm	27,27,28,7
	lvx	30,29,6
	vperm	28,28,29,7
	lvx	31,30,6
	vperm	29,29,30,7
	lvx	22,31,6
	vperm	30,30,31,7
	lvx	24,0,7
	vperm	31,31,22,7
	lvx	25,3,7









	vperm	0,2,4,5
	subi	10,10,31
	vxor	17,8,23
	vsrab	11,8,9
	vaddubm	8,8,8
	vand	11,11,10
	vxor	7,0,17
	xxlor	32+1, 0, 0
	vpermxor	8, 8, 11, 1

	.long	0x7C235699
	vxor	18,8,23
	vsrab	11,8,9
	vaddubm	8,8,8

	vand	11,11,10
	vxor	12,1,18
	xxlor	32+2, 0, 0
	vpermxor	8, 8, 11, 2

	.long	0x7C5A5699
	andi.	31,5,15
	vxor	19,8,23
	vsrab	11,8,9
	vaddubm	8,8,8

	vand	11,11,10
	vxor	13,2,19
	xxlor	32+3, 0, 0
	vpermxor	8, 8, 11, 3

	.long	0x7C7B5699
	sub	5,5,31
	vxor	20,8,23
	vsrab	11,8,9
	vaddubm	8,8,8

	vand	11,11,10
	vxor	14,3,20
	xxlor	32+4, 0, 0
	vpermxor	8, 8, 11, 4

	.long	0x7C9C5699
	subi	5,5,0x60
	vxor	21,8,23
	vsrab	11,8,9
	vaddubm	8,8,8

	vand	11,11,10
	vxor	15,4,21
	xxlor	32+5, 0, 0
	vpermxor	8, 8, 11, 5

	.long	0x7CBD5699
	addi	10,10,0x60
	vxor	22,8,23
	vsrab	11,8,9
	vaddubm	8,8,8

	vand	11,11,10
	vxor	16,5,22
	xxlor	32+0, 0, 0
	vpermxor	8, 8, 11, 0

	vxor	31,31,23
	mtctr	9
	b	Loop_xts_enc6x

.align	5
Loop_xts_enc6x:
	.long	0x10E7C508
	.long	0x118CC508
	.long	0x11ADC508
	.long	0x11CEC508
	.long	0x11EFC508
	.long	0x1210C508
	lvx	24,26,7
	addi	7,7,0x20

	.long	0x10E7CD08
	.long	0x118CCD08
	.long	0x11ADCD08
	.long	0x11CECD08
	.long	0x11EFCD08
	.long	0x1210CD08
	lvx	25,3,7
	bc	16,0,Loop_xts_enc6x

	xxlor	32+10, 1, 1

	subic	5,5,96
	vxor	0,17,31
	.long	0x10E7C508
	.long	0x118CC508
	vsrab	11,8,9
	vxor	17,8,23
	vaddubm	8,8,8
	.long	0x11ADC508
	.long	0x11CEC508
	.long	0x11EFC508
	.long	0x1210C508

	subfe.	0,0,0
	vand	11,11,10
	.long	0x10E7CD08
	.long	0x118CCD08
	xxlor	32+1, 0, 0
	vpermxor	8, 8, 11, 1
	.long	0x11ADCD08
	.long	0x11CECD08
	vxor	1,18,31
	vsrab	11,8,9
	vxor	18,8,23
	.long	0x11EFCD08
	.long	0x1210CD08

	and	0,0,5
	vaddubm	8,8,8
	.long	0x10E7D508
	.long	0x118CD508
	vand	11,11,10
	.long	0x11ADD508
	.long	0x11CED508
	xxlor	32+2, 0, 0
	vpermxor	8, 8, 11, 2
	.long	0x11EFD508
	.long	0x1210D508

	add	10,10,0



	vxor	2,19,31
	vsrab	11,8,9
	vxor	19,8,23
	vaddubm	8,8,8
	.long	0x10E7DD08
	.long	0x118CDD08
	.long	0x11ADDD08
	.long	0x11CEDD08
	vand	11,11,10
	.long	0x11EFDD08
	.long	0x1210DD08

	addi	7,1,64+15
	xxlor	32+3, 0, 0
	vpermxor	8, 8, 11, 3
	.long	0x10E7E508
	.long	0x118CE508
	vxor	3,20,31
	vsrab	11,8,9
	vxor	20,8,23
	.long	0x11ADE508
	.long	0x11CEE508
	vaddubm	8,8,8
	.long	0x11EFE508
	.long	0x1210E508
	lvx	24,0,7
	vand	11,11,10

	.long	0x10E7ED08
	.long	0x118CED08
	xxlor	32+4, 0, 0
	vpermxor	8, 8, 11, 4
	.long	0x11ADED08
	.long	0x11CEED08
	vxor	4,21,31
	vsrab	11,8,9
	vxor	21,8,23
	.long	0x11EFED08
	.long	0x1210ED08
	lvx	25,3,7
	vaddubm	8,8,8

	.long	0x10E7F508
	.long	0x118CF508
	vand	11,11,10
	.long	0x11ADF508
	.long	0x11CEF508
	xxlor	32+5, 0, 0
	vpermxor	8, 8, 11, 5
	.long	0x11EFF508
	.long	0x1210F508
	vxor	5,22,31
	vsrab	11,8,9
	vxor	22,8,23

	.long	0x10E70509
	.long	0x7C005699
	vaddubm	8,8,8
	.long	0x118C0D09
	.long	0x7C235699
	.long	0x11AD1509

	.long	0x7C5A5699
	vand	11,11,10
	.long	0x11CE1D09

	.long	0x7C7B5699
	.long	0x11EF2509

	.long	0x7C9C5699
	xxlor	10, 32+0, 32+0
	xxlor	32+0, 0, 0
	vpermxor	8, 8, 11, 0
	xxlor	32+0, 10, 10
	.long	0x11702D09


	.long	0x7CBD5699
	addi	10,10,0x60





	.long	0x7CE02799
	vxor	7,0,17

	.long	0x7D832799
	vxor	12,1,18

	.long	0x7DBA2799
	vxor	13,2,19

	.long	0x7DDB2799
	vxor	14,3,20

	.long	0x7DFC2799
	vxor	15,4,21

	.long	0x7D7D2799
	vxor	16,5,22
	addi	4,4,0x60

	mtctr	9
	beq	Loop_xts_enc6x

	xxlor	32+10, 2, 2

	addic.	5,5,0x60
	beq	Lxts_enc6x_zero
	cmpwi	5,0x20
	blt	Lxts_enc6x_one
	nop	
	beq	Lxts_enc6x_two
	cmpwi	5,0x40
	blt	Lxts_enc6x_three
	nop	
	beq	Lxts_enc6x_four

Lxts_enc6x_five:
	vxor	7,1,17
	vxor	12,2,18
	vxor	13,3,19
	vxor	14,4,20
	vxor	15,5,21

	bl	_aesp8_xts_enc5x


	vor	17,22,22

	.long	0x7CE02799

	.long	0x7D832799

	.long	0x7DBA2799
	vxor	11,15,22

	.long	0x7DDB2799
	.long	0x7DFC2799
	addi	4,4,0x50
	bne	Lxts_enc6x_steal
	b	Lxts_enc6x_done

.align	4
Lxts_enc6x_four:
	vxor	7,2,17
	vxor	12,3,18
	vxor	13,4,19
	vxor	14,5,20
	vxor	15,15,15

	bl	_aesp8_xts_enc5x


	vor	17,21,21

	.long	0x7CE02799

	.long	0x7D832799
	vxor	11,14,21

	.long	0x7DBA2799
	.long	0x7DDB2799
	addi	4,4,0x40
	bne	Lxts_enc6x_steal
	b	Lxts_enc6x_done

.align	4
Lxts_enc6x_three:
	vxor	7,3,17
	vxor	12,4,18
	vxor	13,5,19
	vxor	14,14,14
	vxor	15,15,15

	bl	_aesp8_xts_enc5x


	vor	17,20,20

	.long	0x7CE02799
	vxor	11,13,20

	.long	0x7D832799
	.long	0x7DBA2799
	addi	4,4,0x30
	bne	Lxts_enc6x_steal
	b	Lxts_enc6x_done

.align	4
Lxts_enc6x_two:
	vxor	7,4,17
	vxor	12,5,18
	vxor	13,13,13
	vxor	14,14,14
	vxor	15,15,15

	bl	_aesp8_xts_enc5x


	vor	17,19,19
	vxor	11,12,19

	.long	0x7CE02799
	.long	0x7D832799
	addi	4,4,0x20
	bne	Lxts_enc6x_steal
	b	Lxts_enc6x_done

.align	4
Lxts_enc6x_one:
	vxor	7,5,17
	nop	
Loop_xts_enc1x:
	.long	0x10E7C508
	lvx	24,26,7
	addi	7,7,0x20

	.long	0x10E7CD08
	lvx	25,3,7
	bc	16,0,Loop_xts_enc1x

	add	10,10,31
	cmpwi	31,0
	.long	0x10E7C508

	subi	10,10,16
	.long	0x10E7CD08

	lvsr	5,0,31
	.long	0x10E7D508

	.long	0x7C005699
	.long	0x10E7DD08

	addi	7,1,64+15
	.long	0x10E7E508
	lvx	24,0,7

	.long	0x10E7ED08
	lvx	25,3,7
	vxor	17,17,31


	.long	0x10E7F508

	vperm	0,0,0,5
	.long	0x10E78D09

	vor	17,18,18
	vxor	11,7,18

	.long	0x7CE02799
	addi	4,4,0x10
	bne	Lxts_enc6x_steal
	b	Lxts_enc6x_done

.align	4
Lxts_enc6x_zero:
	cmpwi	31,0
	beq	Lxts_enc6x_done

	add	10,10,31
	subi	10,10,16
	.long	0x7C005699
	lvsr	5,0,31

	vperm	0,0,0,5
	vxor	11,11,17
Lxts_enc6x_steal:
	vxor	0,0,17
	vxor	7,7,7
	vspltisb	12,-1
	vperm	7,7,12,5
	vsel	7,0,11,7

	subi	30,4,17
	subi	4,4,16
	mtctr	31
Loop_xts_enc6x_steal:
	lbzu	0,1(30)
	stb	0,16(30)
	bc	16,0,Loop_xts_enc6x_steal

	li	31,0
	mtctr	9
	b	Loop_xts_enc1x

.align	4
Lxts_enc6x_done:
	cmpldi	8,0
	beq	Lxts_enc6x_ret

	vxor	8,17,23

	.long	0x7D004799

Lxts_enc6x_ret:
	mtlr	11
	li	10,79
	li	11,95
	stvx	9,10,1
	addi	10,10,32
	stvx	9,11,1
	addi	11,11,32
	stvx	9,10,1
	addi	10,10,32
	stvx	9,11,1
	addi	11,11,32
	stvx	9,10,1
	addi	10,10,32
	stvx	9,11,1
	addi	11,11,32
	stvx	9,10,1
	addi	10,10,32
	stvx	9,11,1
	addi	11,11,32

	or	12,12,12
	lvx	20,10,1
	addi	10,10,32
	lvx	21,11,1
	addi	11,11,32
	lvx	22,10,1
	addi	10,10,32
	lvx	23,11,1
	addi	11,11,32
	lvx	24,10,1
	addi	10,10,32
	lvx	25,11,1
	addi	11,11,32
	lvx	26,10,1
	addi	10,10,32
	lvx	27,11,1
	addi	11,11,32
	lvx	28,10,1
	addi	10,10,32
	lvx	29,11,1
	addi	11,11,32
	lvx	30,10,1
	lvx	31,11,1
	ld	26,400(1)
	ld	27,408(1)
	ld	28,416(1)
	ld	29,424(1)
	ld	30,432(1)
	ld	31,440(1)
	addi	1,1,448
	blr	
.long	0
.byte	0,12,0x04,1,0x80,6,6,0
.long	0

.align	5
_aesp8_xts_enc5x:
	.long	0x10E7C508
	.long	0x118CC508
	.long	0x11ADC508
	.long	0x11CEC508
	.long	0x11EFC508
	lvx	24,26,7
	addi	7,7,0x20

	.long	0x10E7CD08
	.long	0x118CCD08
	.long	0x11ADCD08
	.long	0x11CECD08
	.long	0x11EFCD08
	lvx	25,3,7
	bc	16,0,_aesp8_xts_enc5x

	add	10,10,31
	cmpwi	31,0
	.long	0x10E7C508
	.long	0x118CC508
	.long	0x11ADC508
	.long	0x11CEC508
	.long	0x11EFC508

	subi	10,10,16
	.long	0x10E7CD08
	.long	0x118CCD08
	.long	0x11ADCD08
	.long	0x11CECD08
	.long	0x11EFCD08
	vxor	17,17,31

	.long	0x10E7D508
	lvsr	5,0,31
	.long	0x118CD508
	.long	0x11ADD508
	.long	0x11CED508
	.long	0x11EFD508
	vxor	1,18,31

	.long	0x10E7DD08
	.long	0x7C005699
	.long	0x118CDD08
	.long	0x11ADDD08
	.long	0x11CEDD08
	.long	0x11EFDD08
	vxor	2,19,31

	addi	7,1,64+15
	.long	0x10E7E508
	.long	0x118CE508
	.long	0x11ADE508
	.long	0x11CEE508
	.long	0x11EFE508
	lvx	24,0,7
	vxor	3,20,31

	.long	0x10E7ED08

	.long	0x118CED08
	.long	0x11ADED08
	.long	0x11CEED08
	.long	0x11EFED08
	lvx	25,3,7
	vxor	4,21,31

	.long	0x10E7F508
	vperm	0,0,0,5
	.long	0x118CF508
	.long	0x11ADF508
	.long	0x11CEF508
	.long	0x11EFF508

	.long	0x10E78D09
	.long	0x118C0D09
	.long	0x11AD1509
	.long	0x11CE1D09
	.long	0x11EF2509
	blr	
.long	0
.byte	0,12,0x14,0,0,0,0,0

.align	5
_aesp8_xts_decrypt6x:
	stdu	1,-448(1)
	mflr	11
	li	7,207
	li	3,223
	std	11,464(1)
	stvx	20,7,1
	addi	7,7,32
	stvx	21,3,1
	addi	3,3,32
	stvx	22,7,1
	addi	7,7,32
	stvx	23,3,1
	addi	3,3,32
	stvx	24,7,1
	addi	7,7,32
	stvx	25,3,1
	addi	3,3,32
	stvx	26,7,1
	addi	7,7,32
	stvx	27,3,1
	addi	3,3,32
	stvx	28,7,1
	addi	7,7,32
	stvx	29,3,1
	addi	3,3,32
	stvx	30,7,1
	stvx	31,3,1
	li	0,-1
	stw	12,396(1)
	li	3,0x10
	std	26,400(1)
	li	26,0x20
	std	27,408(1)
	li	27,0x30
	std	28,416(1)
	li	28,0x40
	std	29,424(1)
	li	29,0x50
	std	30,432(1)
	li	30,0x60
	std	31,440(1)
	li	31,0x70
	or	0,0,0


	xxlor	2, 32+10, 32+10
	vsldoi	10,11,10,1
	xxlor	1, 32+10, 32+10


	mr	31, 6
	bl	Lconsts
	lxvw4x	0, 28, 6
	mr	6, 31
	li	31,0x70

	subi	9,9,3

	lvx	23,0,6
	lvx	30,3,6
	addi	6,6,0x20
	lvx	31,0,6
	vperm	23,23,30,7
	addi	7,1,64+15
	mtctr	9

Load_xts_dec_key:
	vperm	24,30,31,7
	lvx	30,3,6
	addi	6,6,0x20
	stvx	24,0,7
	vperm	25,31,30,7
	lvx	31,0,6
	stvx	25,3,7
	addi	7,7,0x20
	bc	16,0,Load_xts_dec_key

	lvx	26,3,6
	vperm	24,30,31,7
	lvx	27,26,6
	stvx	24,0,7
	vperm	25,31,26,7
	lvx	28,27,6
	stvx	25,3,7
	addi	7,1,64+15
	vperm	26,26,27,7
	lvx	29,28,6
	vperm	27,27,28,7
	lvx	30,29,6
	vperm	28,28,29,7
	lvx	31,30,6
	vperm	29,29,30,7
	lvx	22,31,6
	vperm	30,30,31,7
	lvx	24,0,7
	vperm	31,31,22,7
	lvx	25,3,7

	vperm	0,2,4,5
	subi	10,10,31
	vxor	17,8,23
	vsrab	11,8,9
	vaddubm	8,8,8
	vand	11,11,10
	vxor	7,0,17
	xxlor	32+1, 0, 0
	vpermxor	8, 8, 11, 1

	.long	0x7C235699
	vxor	18,8,23
	vsrab	11,8,9
	vaddubm	8,8,8

	vand	11,11,10
	vxor	12,1,18
	xxlor	32+2, 0, 0
	vpermxor	8, 8, 11, 2

	.long	0x7C5A5699
	andi.	31,5,15
	vxor	19,8,23
	vsrab	11,8,9
	vaddubm	8,8,8

	vand	11,11,10
	vxor	13,2,19
	xxlor	32+3, 0, 0
	vpermxor	8, 8, 11, 3

	.long	0x7C7B5699
	sub	5,5,31
	vxor	20,8,23
	vsrab	11,8,9
	vaddubm	8,8,8

	vand	11,11,10
	vxor	14,3,20
	xxlor	32+4, 0, 0
	vpermxor	8, 8, 11, 4

	.long	0x7C9C5699
	subi	5,5,0x60
	vxor	21,8,23
	vsrab	11,8,9
	vaddubm	8,8,8

	vand	11,11,10
	vxor	15,4,21
	xxlor	32+5, 0, 0
	vpermxor	8, 8, 11, 5

	.long	0x7CBD5699
	addi	10,10,0x60
	vxor	22,8,23
	vsrab	11,8,9
	vaddubm	8,8,8

	vand	11,11,10
	vxor	16,5,22
	xxlor	32+0, 0, 0
	vpermxor	8, 8, 11, 0

	vxor	31,31,23
	mtctr	9
	b	Loop_xts_dec6x

.align	5
Loop_xts_dec6x:
	.long	0x10E7C548
	.long	0x118CC548
	.long	0x11ADC548
	.long	0x11CEC548
	.long	0x11EFC548
	.long	0x1210C548
	lvx	24,26,7
	addi	7,7,0x20

	.long	0x10E7CD48
	.long	0x118CCD48
	.long	0x11ADCD48
	.long	0x11CECD48
	.long	0x11EFCD48
	.long	0x1210CD48
	lvx	25,3,7
	bc	16,0,Loop_xts_dec6x

	xxlor	32+10, 1, 1

	subic	5,5,96
	vxor	0,17,31
	.long	0x10E7C548
	.long	0x118CC548
	vsrab	11,8,9
	vxor	17,8,23
	vaddubm	8,8,8
	.long	0x11ADC548
	.long	0x11CEC548
	.long	0x11EFC548
	.long	0x1210C548

	subfe.	0,0,0
	vand	11,11,10
	.long	0x10E7CD48
	.long	0x118CCD48
	xxlor	32+1, 0, 0
	vpermxor	8, 8, 11, 1
	.long	0x11ADCD48
	.long	0x11CECD48
	vxor	1,18,31
	vsrab	11,8,9
	vxor	18,8,23
	.long	0x11EFCD48
	.long	0x1210CD48

	and	0,0,5
	vaddubm	8,8,8
	.long	0x10E7D548
	.long	0x118CD548
	vand	11,11,10
	.long	0x11ADD548
	.long	0x11CED548
	xxlor	32+2, 0, 0
	vpermxor	8, 8, 11, 2
	.long	0x11EFD548
	.long	0x1210D548

	add	10,10,0



	vxor	2,19,31
	vsrab	11,8,9
	vxor	19,8,23
	vaddubm	8,8,8
	.long	0x10E7DD48
	.long	0x118CDD48
	.long	0x11ADDD48
	.long	0x11CEDD48
	vand	11,11,10
	.long	0x11EFDD48
	.long	0x1210DD48

	addi	7,1,64+15
	xxlor	32+3, 0, 0
	vpermxor	8, 8, 11, 3
	.long	0x10E7E548
	.long	0x118CE548
	vxor	3,20,31
	vsrab	11,8,9
	vxor	20,8,23
	.long	0x11ADE548
	.long	0x11CEE548
	vaddubm	8,8,8
	.long	0x11EFE548
	.long	0x1210E548
	lvx	24,0,7
	vand	11,11,10

	.long	0x10E7ED48
	.long	0x118CED48
	xxlor	32+4, 0, 0
	vpermxor	8, 8, 11, 4
	.long	0x11ADED48
	.long	0x11CEED48
	vxor	4,21,31
	vsrab	11,8,9
	vxor	21,8,23
	.long	0x11EFED48
	.long	0x1210ED48
	lvx	25,3,7
	vaddubm	8,8,8

	.long	0x10E7F548
	.long	0x118CF548
	vand	11,11,10
	.long	0x11ADF548
	.long	0x11CEF548
	xxlor	32+5, 0, 0
	vpermxor	8, 8, 11, 5
	.long	0x11EFF548
	.long	0x1210F548
	vxor	5,22,31
	vsrab	11,8,9
	vxor	22,8,23

	.long	0x10E70549
	.long	0x7C005699
	vaddubm	8,8,8
	.long	0x118C0D49
	.long	0x7C235699
	.long	0x11AD1549

	.long	0x7C5A5699
	vand	11,11,10
	.long	0x11CE1D49

	.long	0x7C7B5699
	.long	0x11EF2549

	.long	0x7C9C5699
	xxlor	10, 32+0, 32+0
	xxlor	32+0, 0, 0
	vpermxor	8, 8, 11, 0
	xxlor	32+0, 10, 10
	.long	0x12102D49

	.long	0x7CBD5699
	addi	10,10,0x60





	.long	0x7CE02799
	vxor	7,0,17

	.long	0x7D832799
	vxor	12,1,18

	.long	0x7DBA2799
	vxor	13,2,19

	.long	0x7DDB2799
	vxor	14,3,20

	.long	0x7DFC2799
	vxor	15,4,21
	.long	0x7E1D2799
	vxor	16,5,22
	addi	4,4,0x60

	mtctr	9
	beq	Loop_xts_dec6x

	xxlor	32+10, 2, 2

	addic.	5,5,0x60
	beq	Lxts_dec6x_zero
	cmpwi	5,0x20
	blt	Lxts_dec6x_one
	nop	
	beq	Lxts_dec6x_two
	cmpwi	5,0x40
	blt	Lxts_dec6x_three
	nop	
	beq	Lxts_dec6x_four

Lxts_dec6x_five:
	vxor	7,1,17
	vxor	12,2,18
	vxor	13,3,19
	vxor	14,4,20
	vxor	15,5,21

	bl	_aesp8_xts_dec5x


	vor	17,22,22
	vxor	18,8,23

	.long	0x7CE02799
	vxor	7,0,18

	.long	0x7D832799

	.long	0x7DBA2799

	.long	0x7DDB2799
	.long	0x7DFC2799
	addi	4,4,0x50
	bne	Lxts_dec6x_steal
	b	Lxts_dec6x_done

.align	4
Lxts_dec6x_four:
	vxor	7,2,17
	vxor	12,3,18
	vxor	13,4,19
	vxor	14,5,20
	vxor	15,15,15

	bl	_aesp8_xts_dec5x


	vor	17,21,21
	vor	18,22,22

	.long	0x7CE02799
	vxor	7,0,22

	.long	0x7D832799

	.long	0x7DBA2799
	.long	0x7DDB2799
	addi	4,4,0x40
	bne	Lxts_dec6x_steal
	b	Lxts_dec6x_done

.align	4
Lxts_dec6x_three:
	vxor	7,3,17
	vxor	12,4,18
	vxor	13,5,19
	vxor	14,14,14
	vxor	15,15,15

	bl	_aesp8_xts_dec5x


	vor	17,20,20
	vor	18,21,21

	.long	0x7CE02799
	vxor	7,0,21

	.long	0x7D832799
	.long	0x7DBA2799
	addi	4,4,0x30
	bne	Lxts_dec6x_steal
	b	Lxts_dec6x_done

.align	4
Lxts_dec6x_two:
	vxor	7,4,17
	vxor	12,5,18
	vxor	13,13,13
	vxor	14,14,14
	vxor	15,15,15

	bl	_aesp8_xts_dec5x


	vor	17,19,19
	vor	18,20,20

	.long	0x7CE02799
	vxor	7,0,20
	.long	0x7D832799
	addi	4,4,0x20
	bne	Lxts_dec6x_steal
	b	Lxts_dec6x_done

.align	4
Lxts_dec6x_one:
	vxor	7,5,17
	nop	
Loop_xts_dec1x:
	.long	0x10E7C548
	lvx	24,26,7
	addi	7,7,0x20

	.long	0x10E7CD48
	lvx	25,3,7
	bc	16,0,Loop_xts_dec1x

	subi	0,31,1
	.long	0x10E7C548

	andi.	0,0,16
	cmpwi	31,0
	.long	0x10E7CD48

	sub	10,10,0
	.long	0x10E7D548

	.long	0x7C005699
	.long	0x10E7DD48

	addi	7,1,64+15
	.long	0x10E7E548
	lvx	24,0,7

	.long	0x10E7ED48
	lvx	25,3,7
	vxor	17,17,31


	.long	0x10E7F548

	mtctr	9
	.long	0x10E78D49

	vor	17,18,18
	vor	18,19,19

	.long	0x7CE02799
	addi	4,4,0x10
	vxor	7,0,19
	bne	Lxts_dec6x_steal
	b	Lxts_dec6x_done

.align	4
Lxts_dec6x_zero:
	cmpwi	31,0
	beq	Lxts_dec6x_done

	.long	0x7C005699

	vxor	7,0,18
Lxts_dec6x_steal:
	.long	0x10E7C548
	lvx	24,26,7
	addi	7,7,0x20

	.long	0x10E7CD48
	lvx	25,3,7
	bc	16,0,Lxts_dec6x_steal

	add	10,10,31
	.long	0x10E7C548

	cmpwi	31,0
	.long	0x10E7CD48

	.long	0x7C005699
	.long	0x10E7D548

	lvsr	5,0,31
	.long	0x10E7DD48

	addi	7,1,64+15
	.long	0x10E7E548
	lvx	24,0,7

	.long	0x10E7ED48
	lvx	25,3,7
	vxor	18,18,31


	.long	0x10E7F548

	vperm	0,0,0,5
	.long	0x11679549



	.long	0x7D602799

	vxor	7,7,7
	vspltisb	12,-1
	vperm	7,7,12,5
	vsel	7,0,11,7
	vxor	7,7,17

	subi	30,4,1
	mtctr	31
Loop_xts_dec6x_steal:
	lbzu	0,1(30)
	stb	0,16(30)
	bc	16,0,Loop_xts_dec6x_steal

	li	31,0
	mtctr	9
	b	Loop_xts_dec1x

.align	4
Lxts_dec6x_done:
	cmpldi	8,0
	beq	Lxts_dec6x_ret

	vxor	8,17,23

	.long	0x7D004799

Lxts_dec6x_ret:
	mtlr	11
	li	10,79
	li	11,95
	stvx	9,10,1
	addi	10,10,32
	stvx	9,11,1
	addi	11,11,32
	stvx	9,10,1
	addi	10,10,32
	stvx	9,11,1
	addi	11,11,32
	stvx	9,10,1
	addi	10,10,32
	stvx	9,11,1
	addi	11,11,32
	stvx	9,10,1
	addi	10,10,32
	stvx	9,11,1
	addi	11,11,32

	or	12,12,12
	lvx	20,10,1
	addi	10,10,32
	lvx	21,11,1
	addi	11,11,32
	lvx	22,10,1
	addi	10,10,32
	lvx	23,11,1
	addi	11,11,32
	lvx	24,10,1
	addi	10,10,32
	lvx	25,11,1
	addi	11,11,32
	lvx	26,10,1
	addi	10,10,32
	lvx	27,11,1
	addi	11,11,32
	lvx	28,10,1
	addi	10,10,32
	lvx	29,11,1
	addi	11,11,32
	lvx	30,10,1
	lvx	31,11,1
	ld	26,400(1)
	ld	27,408(1)
	ld	28,416(1)
	ld	29,424(1)
	ld	30,432(1)
	ld	31,440(1)
	addi	1,1,448
	blr	
.long	0
.byte	0,12,0x04,1,0x80,6,6,0
.long	0

.align	5
_aesp8_xts_dec5x:
	.long	0x10E7C548
	.long	0x118CC548
	.long	0x11ADC548
	.long	0x11CEC548
	.long	0x11EFC548
	lvx	24,26,7
	addi	7,7,0x20

	.long	0x10E7CD48
	.long	0x118CCD48
	.long	0x11ADCD48
	.long	0x11CECD48
	.long	0x11EFCD48
	lvx	25,3,7
	bc	16,0,_aesp8_xts_dec5x

	subi	0,31,1
	.long	0x10E7C548
	.long	0x118CC548
	.long	0x11ADC548
	.long	0x11CEC548
	.long	0x11EFC548

	andi.	0,0,16
	cmpwi	31,0
	.long	0x10E7CD48
	.long	0x118CCD48
	.long	0x11ADCD48
	.long	0x11CECD48
	.long	0x11EFCD48
	vxor	17,17,31

	sub	10,10,0
	.long	0x10E7D548
	.long	0x118CD548
	.long	0x11ADD548
	.long	0x11CED548
	.long	0x11EFD548
	vxor	1,18,31

	.long	0x10E7DD48
	.long	0x7C005699
	.long	0x118CDD48
	.long	0x11ADDD48
	.long	0x11CEDD48
	.long	0x11EFDD48
	vxor	2,19,31

	addi	7,1,64+15
	.long	0x10E7E548
	.long	0x118CE548
	.long	0x11ADE548
	.long	0x11CEE548
	.long	0x11EFE548
	lvx	24,0,7
	vxor	3,20,31

	.long	0x10E7ED48

	.long	0x118CED48
	.long	0x11ADED48
	.long	0x11CEED48
	.long	0x11EFED48
	lvx	25,3,7
	vxor	4,21,31

	.long	0x10E7F548
	.long	0x118CF548
	.long	0x11ADF548
	.long	0x11CEF548
	.long	0x11EFF548

	.long	0x10E78D49
	.long	0x118C0D49
	.long	0x11AD1549
	.long	0x11CE1D49
	.long	0x11EF2549
	mtctr	9
	blr	
.long	0
.byte	0,12,0x14,0,0,0,0,0
