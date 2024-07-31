.machine	"any"
.abiversion	2
.text
.globl	poly1305_init_int
.type	poly1305_init_int,@function
.align	4
poly1305_init_int:
.localentry	poly1305_init_int,0

	xor	0,0,0
	std	0,0(3)
	std	0,8(3)
	std	0,16(3)
	stw	0,24(3)

	cmpld	4,0
	beq-	.Lno_key
	ld	10,0(4)
	ld	11,8(4)
	lis	8,0xfff
	ori	8,8,0xfffc
	insrdi	8,8,32,0
	ori	7,8,3

	and	10,10,7
	and	11,11,8

	std	10,32(3)
	std	11,40(3)

.Lno_key:
	xor	3,3,3
	blr	
.long	0
.byte	0,12,0x14,0,0,0,2,0
.size	poly1305_init_int,.-poly1305_init_int

.globl	poly1305_blocks
.type	poly1305_blocks,@function
.align	4
poly1305_blocks:
.localentry	poly1305_blocks,0

.Lpoly1305_blocks:
	srdi.	5,5,4
	beq-	.Labort

	stdu	1,-192(1)
	mflr	0
	std	27,152(1)
	std	28,160(1)
	std	29,168(1)
	std	30,176(1)
	std	31,184(1)
	std	0,208(1)

	ld	27,32(3)
	ld	28,40(3)

	ld	7,0(3)
	ld	8,8(3)
	ld	9,16(3)

	srdi	29,28,2
	mtctr	5
	add	29,29,28
	li	0,3
	b	.Loop

.align	4
.Loop:
	ld	30,0(4)
	ld	31,8(4)
	addi	4,4,16

	addc	7,7,30
	adde	8,8,31

	mulld	10,7,27
	mulhdu	11,7,27
	adde	9,9,6

	mulld	30,8,29
	mulhdu	31,8,29
	addc	10,10,30
	adde	11,11,31

	mulld	30,7,28
	mulhdu	12,7,28
	addc	11,11,30
	addze	12,12

	mulld	30,8,27
	mulhdu	31,8,27
	addc	11,11,30
	adde	12,12,31

	mulld	30,9,29
	mulld	31,9,27
	addc	11,11,30
	adde	12,12,31

	andc	30,12,0
	and	9,12,0
	srdi	31,30,2
	add	30,30,31
	addc	7,10,30
	addze	8,11
	addze	9,9

	bdnz	.Loop

	std	7,0(3)
	std	8,8(3)
	std	9,16(3)

	ld	27,152(1)
	ld	28,160(1)
	ld	29,168(1)
	ld	30,176(1)
	ld	31,184(1)
	addi	1,1,192
.Labort:
	blr	
.long	0
.byte	0,12,4,1,0x80,5,4,0
.size	poly1305_blocks,.-poly1305_blocks
.globl	poly1305_emit
.type	poly1305_emit,@function
.align	5
poly1305_emit:
.localentry	poly1305_emit,0

	lwz	7,0(3)
	lwz	8,4(3)
	lwz	9,8(3)
	lwz	10,12(3)
	lwz	11,16(3)
	lwz	0,24(3)

	sldi	8,8,26
	sldi	12,9,52
	srdi	9,9,12
	sldi	10,10,14
	add	7,7,8
	addc	7,7,12
	sldi	12,11,40
	srdi	11,11,24
	adde	8,9,10
	addc	8,8,12
	addze	9,11

	ld	10,0(3)
	ld	11,8(3)
	ld	12,16(3)

	neg	0,0
	xor	7,7,10
	xor	8,8,11
	xor	9,9,12
	and	7,7,0
	and	8,8,0
	and	9,9,0
	xor	7,7,10
	xor	8,8,11
	xor	9,9,12

	addic	10,7,5
	addze	11,8
	addze	12,9

	srdi	12,12,2
	neg	12,12

	andc	7,7,12
	and	10,10,12
	andc	8,8,12
	and	11,11,12
	or	7,7,10
	or	8,8,11

	lwz	12,4(5)
	lwz	9,12(5)
	lwz	10,0(5)
	lwz	11,8(5)

	insrdi	10,12,32,0
	insrdi	11,9,32,0

	addc	7,7,10
	adde	8,8,11

	addi	3,4,-1
	addi	4,4,7

	stbu	7,1(3)
	srdi	7,7,8
	stbu	8,1(4)
	srdi	8,8,8

	stbu	7,1(3)
	srdi	7,7,8
	stbu	8,1(4)
	srdi	8,8,8

	stbu	7,1(3)
	srdi	7,7,8
	stbu	8,1(4)
	srdi	8,8,8

	stbu	7,1(3)
	srdi	7,7,8
	stbu	8,1(4)
	srdi	8,8,8

	stbu	7,1(3)
	srdi	7,7,8
	stbu	8,1(4)
	srdi	8,8,8

	stbu	7,1(3)
	srdi	7,7,8
	stbu	8,1(4)
	srdi	8,8,8

	stbu	7,1(3)
	srdi	7,7,8
	stbu	8,1(4)
	srdi	8,8,8

	stbu	7,1(3)
	stbu	8,1(4)

	blr	
.long	0
.byte	0,12,0x14,0,0,0,3,0
.size	poly1305_emit,.-poly1305_emit
.globl	poly1305_blocks_vsx
.type	poly1305_blocks_vsx,@function
.align	5
poly1305_blocks_vsx:
.localentry	poly1305_blocks_vsx,0

	lwz	7,24(3)
	cmpldi	5,128
	bge	__poly1305_blocks_vsx

	neg	0,7
	lwz	7,0(3)
	lwz	8,4(3)
	lwz	9,8(3)
	lwz	10,12(3)
	lwz	11,16(3)

	sldi	8,8,26
	sldi	12,9,52
	add	7,7,8
	srdi	9,9,12
	sldi	10,10,14
	addc	7,7,12
	sldi	8,11,40
	adde	9,9,10
	srdi	11,11,24
	addc	9,9,8
	addze	11,11

	ld	8,0(3)
	ld	10,8(3)
	ld	12,16(3)

	xor	7,7,8
	xor	9,9,10
	xor	11,11,12
	and	7,7,0
	and	9,9,0
	and	11,11,0
	xor	7,7,8
	xor	9,9,10
	xor	11,11,12

	li	0,0
	std	7,0(3)
	std	9,8(3)
	std	11,16(3)
	stw	0,24(3)

	b	.Lpoly1305_blocks
.long	0
.byte	0,12,0x14,0,0,0,4,0
.size	poly1305_blocks_vsx,.-poly1305_blocks_vsx

.align	5
__poly1305_mul:
	mulld	9,6,27
	mulhdu	10,6,27

	mulld	30,7,29
	mulhdu	31,7,29
	addc	9,9,30
	adde	10,10,31

	mulld	30,6,28
	mulhdu	11,6,28
	addc	10,10,30
	addze	11,11

	mulld	30,7,27
	mulhdu	31,7,27
	addc	10,10,30
	adde	11,11,31

	mulld	30,8,29
	mulld	31,8,27
	addc	10,10,30
	adde	11,11,31

	andc	30,11,0
	and	8,11,0
	srdi	31,30,2
	add	30,30,31
	addc	6,9,30
	addze	7,10
	addze	8,8

	blr	
.long	0
.byte	0,12,0x14,0,0,0,0,0
.size	__poly1305_mul,.-__poly1305_mul

.align	5
__poly1305_splat:
	rldicl	9,6,0,38
	rldicl	10,6,38,38
	stw	9,0x00(31)

	rldicl	11,6,12,52
	slwi	9,10,2
	stw	10,0x10(31)
	add	9,9,10
	stw	9,0x20(31)

	insrdi	11,7,14,38
	slwi	9,11,2
	stw	11,0x30(31)
	add	9,9,11
	stw	9,0x40(31)

	rldicl	10,7,50,38
	rldicl	11,7,24,40
	slwi	9,10,2
	stw	10,0x50(31)
	add	9,9,10
	stw	9,0x60(31)

	insrdi	11,8,3,37
	slwi	9,11,2
	stw	11,0x70(31)
	add	9,9,11
	stw	9,0x80(31)

	blr	
.long	0
.byte	0,12,0x14,0,0,0,0,0
.size	__poly1305_splat,.-__poly1305_splat

.align	5
__poly1305_blocks_vsx:
	stdu	1,-416(1)
	mflr	0
	li	10,191
	li	11,207
	li	12,-1
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
	stw	12,372(1)
	li	12,-1
	or	12,12,12
	std	27,376(1)
	std	28,384(1)
	std	29,392(1)
	std	30,400(1)
	std	31,408(1)
	std	0,432(1)

	bl	.LPICmeup

	li	27,0x10
	li	28,0x20
	li	29,0x30
	li	30,0x40
	li	31,0x50
	.long	0x7FA06699
	.long	0x7F3B6699
	.long	0x7F7C6699
	.long	0x7FFD6699
	.long	0x7FDE6699

	cmplwi	7,0
	bne	.Lskip_init_vsx

	ld	27,32(3)
	ld	28,40(3)
	srdi	29,28,2
	li	0,3
	add	29,29,28

	mr	6,27
	mr	7,28
	li	8,0
	addi	31,3,60
	bl	__poly1305_splat

	bl	__poly1305_mul
	addi	31,3,52
	bl	__poly1305_splat

	bl	__poly1305_mul
	addi	31,3,56
	bl	__poly1305_splat

	bl	__poly1305_mul
	addi	31,3,48
	bl	__poly1305_splat

	ld	6,0(3)
	ld	7,8(3)
	ld	8,16(3)

	rldicl	9,6,0,38
	rldicl	10,6,38,38
	rldicl	11,6,12,52
	.long	0x7C0901E7
	insrdi	11,7,14,38
	.long	0x7C2A01E7
	rldicl	10,7,50,38
	.long	0x7C4B01E7
	rldicl	11,7,24,40
	.long	0x7C6A01E7
	insrdi	11,8,3,37
	.long	0x7C8B01E7
	li	0,1
	stw	0,24(3)
	b	.Loaded_vsx

.align	4
.Lskip_init_vsx:
	li	27,4
	li	28,8
	li	29,12
	li	30,16
	.long	0x7C001819
	.long	0x7C3B1819
	.long	0x7C5C1819
	.long	0x7C7D1819
	.long	0x7C9E1819

.Loaded_vsx:
	li	27,0x10
	li	28,0x20
	li	29,0x30
	li	30,0x40
	li	31,0x50
	li	7,0x60
	li	8,0x70
	addi	10,3,64
	addi	11,1,63

	vxor	20,20,20
	.long	0xF000A057
	.long	0xF021A057
	.long	0xF042A057
	.long	0xF063A057
	.long	0xF084A057


	.long	0x7EA02699
	.long	0x7EDB2699
	.long	0x7EFC2699
	.long	0x7F1D2699





	.long	0xF0B5B057
	vspltisb	26,4
	vperm	7,21,22,31
	vspltisb	28,14
	.long	0xF115B357

	.long	0x10C5CEC4
	.long	0x10E7D6C4
	.long	0x1128DEC4
	.long	0x1108E6C4
	vand	5,5,29
	vand	6,6,29
	vand	7,7,29
	vand	8,8,29

	.long	0xF2B7C057
	vperm	22,23,24,31
	.long	0xF2F7C357

	.long	0x1295CEC4
	.long	0x12D6D6C4
	.long	0x1317DEC4
	.long	0x12F7E6C4
	vand	21,21,29
	vand	20,20,29
	vand	22,22,29
	vand	23,23,29


	.long	0x11384E8C
	.long	0x10B52E8C
	.long	0x10D4368C
	.long	0x10F63E8C
	.long	0x1117468C
	vor	9,9,30

	.long	0x7D5D1A99
	.long	0x7D605299
	.long	0x7D9B5299
	.long	0x7DBC5299
	.long	0x7DDD5299
	.long	0x7EBE5299
	.long	0x7EDF5299
	.long	0x7EE75299
	.long	0x7F085299
	stvx	11,0,11
	stvx	12,27,11
	stvx	13,28,11
	stvx	14,29,11
	stvx	21,30,11
	stvx	22,31,11
	stvx	23,7,11
	stvx	24,8,11

	addi	4,4,0x40
	addi	12,12,0x50
	addi	0,5,-64
	srdi	0,0,6
	mtctr	0
	b	.Loop_vsx

.align	4
.Loop_vsx:














	.long	0x11E55288
	.long	0x12055A88
	.long	0x12256A88
	.long	0x12466A88

	.long	0x12865288
	.long	0x1210A0C0
	.long	0x12865A88
	.long	0x1231A0C0
	.long	0x12676A88
	.long	0x12896288
	.long	0x11EFA0C0
	.long	0x12875A88
	.long	0x1252A0C0
	lvx	12,31,11
	.long	0x12885A88
	.long	0x1273A0C0
	lvx	11,30,11

	.long	0x104238C0
	.long	0x100028C0
	.long	0x106340C0
	.long	0x102130C0
	.long	0x108448C0

	.long	0x12887288
	.long	0x11EFA0C0
	.long	0x12897288
	.long	0x1210A0C0
	.long	0x12875288
	.long	0x1231A0C0
	.long	0x12885288
	.long	0x1252A0C0
	lvx	14,8,11
	.long	0x12895288
	.long	0x1273A0C0
	lvx	13,7,11

	.long	0x12876288
	.long	0x11EFA0C0
	.long	0x12886288
	.long	0x1210A0C0
	.long	0x12896288
	.long	0x1231A0C0
	.long	0x12855A88
	.long	0x1252A0C0
	.long	0x12865A88
	.long	0x1273A0C0


	.long	0x7EA02699
	.long	0x7EDB2699
	.long	0x7EFC2699
	.long	0x7F1D2699





	.long	0x12867288
	.long	0x11EFA0C0
	.long	0x12877288
	.long	0x1210A0C0
	.long	0x12887288
	.long	0x1231A0C0
	.long	0x12897288
	.long	0x1252A0C0
	.long	0x12856A88
	.long	0x1273A0C0

	.long	0xF0B5B057
	vspltisb	26,4
	vperm	7,21,22,31
	.long	0xF115B357


	.long	0x12805088
	.long	0x11EFA0C0
	.long	0x12815088
	.long	0x1210A0C0
	.long	0x12825088
	.long	0x1231A0C0
	.long	0x12835088
	.long	0x1252A0C0
	.long	0x12845088
	.long	0x1273A0C0

	.long	0xF2B7C057
	vperm	22,23,24,31
	.long	0xF2F7C357

	.long	0x12826088
	.long	0x11EFA0C0
	.long	0x12836088
	.long	0x1210A0C0
	.long	0x12846088
	.long	0x1231A0C0
	.long	0x12805888
	.long	0x1252A0C0
	lvx	12,27,11
	.long	0x12815888
	.long	0x1273A0C0
	lvx	11,0,11

	.long	0x10C5CEC4
	.long	0x10E7D6C4
	.long	0x1128DEC4
	.long	0x1108E6C4

	.long	0x12817088
	.long	0x11EFA0C0
	.long	0x12827088
	.long	0x1210A0C0
	.long	0x12837088
	.long	0x1231A0C0
	.long	0x12847088
	.long	0x1252A0C0
	lvx	14,29,11
	.long	0x12806888
	.long	0x1273A0C0
	lvx	13,28,11

	vand	5,5,29
	vand	6,6,29
	vand	7,7,29
	vand	8,8,29

	.long	0x12846088
	.long	0x11EFA0C0
	.long	0x12805888
	.long	0x1210A0C0
	.long	0x12815888
	.long	0x1231A0C0
	.long	0x12825888
	.long	0x1252A0C0
	.long	0x12835888
	.long	0x1273A0C0

	.long	0x12D6D6C4
	.long	0x1355CEC4
	.long	0x1317DEC4
	.long	0x12F7E6C4

	.long	0x12837088
	.long	0x11EFA0C0
	.long	0x12847088
	.long	0x1210A0C0
	.long	0x12806888
	.long	0x1231A0C0
	.long	0x12816888
	.long	0x1252A0C0
	.long	0x12826888
	.long	0x1273A0C0

	vand	21,21,29
	vand	26,26,29
	vand	22,22,29
	vand	23,23,29





	vspltisb	20,2
	.long	0x1092CEC4
	.long	0x102FCEC4
	vand	3,18,29
	vand	0,15,29
	.long	0x108498C0
	.long	0x102180C0

	.long	0x11384E8C
	.long	0x10B52E8C
	.long	0x10DA368C
	.long	0x10F63E8C
	.long	0x1117468C
	vor	9,9,30

	.long	0x1264CEC4
	.long	0x1201CEC4
	vand	4,4,29
	vand	1,1,29
	.long	0x100098C0
	.long	0x105180C0

	.long	0x1273A5C4
	.long	0x1222CEC4
	vand	2,2,29
	.long	0x100098C0
	.long	0x106388C0

	.long	0x11E0CEC4
	.long	0x1243CEC4
	vand	0,0,29
	vand	3,3,29
	.long	0x102178C0
	.long	0x108490C0

	addi	4,4,0x40
	bdnz	.Loop_vsx

	neg	5,5
	andi.	5,5,0x30
	sub	4,4,5

	.long	0x7D5D1E99
	.long	0x7D605699
	.long	0x7D9B5699
	.long	0x7DBC5699
	.long	0x7DDD5699

.Last_vsx:
	.long	0x11E55288
	.long	0x12065288
	.long	0x12275288
	.long	0x12485288
	.long	0x12695288

	.long	0x12896288
	.long	0x11EFA0C0
	.long	0x12855A88
	.long	0x1210A0C0
	.long	0x12865A88
	.long	0x1231A0C0
	.long	0x12875A88
	.long	0x1252A0C0
	.long	0x7D9F5699
	.long	0x12885A88
	.long	0x1273A0C0
	.long	0x7D7E5699

	.long	0x104238C0
	.long	0x100028C0
	.long	0x106340C0
	.long	0x102130C0
	.long	0x108448C0

	.long	0x12887288
	.long	0x11EFA0C0
	.long	0x12897288
	.long	0x1210A0C0
	.long	0x12856A88
	.long	0x1231A0C0
	.long	0x12866A88
	.long	0x1252A0C0
	.long	0x7DC85699
	.long	0x12876A88
	.long	0x1273A0C0
	.long	0x7DA75699

	.long	0x12876288
	.long	0x11EFA0C0
	.long	0x12886288
	.long	0x1210A0C0
	.long	0x12896288
	.long	0x1231A0C0
	.long	0x12855A88
	.long	0x1252A0C0
	.long	0x12865A88
	.long	0x1273A0C0

	.long	0x12867288
	.long	0x11EFA0C0
	.long	0x12877288
	.long	0x1210A0C0
	.long	0x12887288
	.long	0x1231A0C0
	.long	0x12897288
	.long	0x1252A0C0
	.long	0x12856A88
	.long	0x1273A0C0


	.long	0x12805088
	.long	0x11EFA0C0
	.long	0x12815088
	.long	0x1210A0C0
	.long	0x12825088
	.long	0x1231A0C0
	.long	0x12835088
	.long	0x1252A0C0
	.long	0x12845088
	.long	0x1273A0C0

	.long	0x12826088
	.long	0x11EFA0C0
	.long	0x12836088
	.long	0x1210A0C0
	.long	0x12846088
	.long	0x1231A0C0
	.long	0x12805888
	.long	0x1252A0C0
	.long	0x7D9B5699
	.long	0x12815888
	.long	0x1273A0C0
	.long	0x7D605699

	.long	0x12817088
	.long	0x11EFA0C0
	.long	0x12827088
	.long	0x1210A0C0
	.long	0x12837088
	.long	0x1231A0C0
	.long	0x12847088
	.long	0x1252A0C0
	.long	0x7DDD5699
	.long	0x12806888
	.long	0x1273A0C0
	.long	0x7DBC5699

	.long	0x12846088
	.long	0x11EFA0C0
	.long	0x12805888
	.long	0x1210A0C0
	.long	0x12815888
	.long	0x1231A0C0
	.long	0x12825888
	.long	0x1252A0C0
	.long	0x12835888
	.long	0x1273A0C0

	.long	0x12837088
	.long	0x11EFA0C0
	.long	0x12847088
	.long	0x1210A0C0
	.long	0x12806888
	.long	0x1231A0C0
	.long	0x12816888
	.long	0x1252A0C0
	.long	0x12826888
	.long	0x1273A0C0




	.long	0xF00F7A57
	.long	0xF0308257
	.long	0xF0518A57
	.long	0xF0729257
	.long	0xF0939A57
	.long	0x11EF00C0
	.long	0x121008C0
	.long	0x123110C0
	.long	0x125218C0
	.long	0x127320C0




	vspltisb	20,2
	.long	0x1092CEC4
	.long	0x102FCEC4
	vand	3,18,29
	vand	0,15,29
	.long	0x108498C0
	.long	0x102180C0

	.long	0x1264CEC4
	.long	0x1201CEC4
	vand	4,4,29
	vand	1,1,29
	.long	0x100098C0
	.long	0x105180C0

	.long	0x1273A5C4
	.long	0x1222CEC4
	vand	2,2,29
	.long	0x100098C0
	.long	0x106388C0

	.long	0x11E0CEC4
	.long	0x1243CEC4
	vand	0,0,29
	vand	3,3,29
	.long	0x102178C0
	.long	0x108490C0

	beq	.Ldone_vsx

	add	6,12,5


	.long	0x7EA02699
	.long	0x7EDB2699
	.long	0x7EFC2699
	.long	0x7F1D2699





	.long	0xF0B5B057
	vspltisb	26,4
	vperm	7,21,22,31
	.long	0xF115B357

	.long	0x10C5CEC4
	.long	0x10E7D6C4
	.long	0x1128DEC4
	.long	0x1108E6C4
	vand	5,5,29
	vand	6,6,29
	vand	7,7,29
	vand	8,8,29

	.long	0xF297C057
	vperm	21,23,24,31
	.long	0xF2D7C357

	.long	0x7DE03699
	.long	0x7E1D3699

	.long	0x12F4CEC4
	.long	0x12B5D6C4
	.long	0x1316DEC4
	.long	0x12D6E6C4
	vand	20,20,29
	vand	23,23,29
	vand	21,21,29
	vand	22,22,29


	.long	0x11384E8C
	.long	0x10B42E8C
	.long	0x10D7368C
	.long	0x10F53E8C
	.long	0x1116468C
	vor	9,9,30

	vperm	0,0,0,15
	vand	5,5,    16
	vperm	1,1,1,15
	vand	6,6,    16
	vperm	2,2,2,15
	vand	7,7,    16
	vperm	3,3,3,15
	vand	8,8,    16
	vperm	4,4,4,15
	vand	9,9,    16

	.long	0x10A500C0
	vxor	0,0,0
	.long	0x10C608C0
	vxor	1,1,1
	.long	0x10E710C0
	vxor	2,2,2
	.long	0x110818C0
	vxor	3,3,3
	.long	0x112920C0
	vxor	4,4,4

	xor.	5,5,5
	b	.Last_vsx

.align	4
.Ldone_vsx:
	ld	0,432(1)
	li	27,4
	li	28,8
	li	29,12
	li	30,16
	.long	0x7C001919
	.long	0x7C3B1919
	.long	0x7C5C1919
	.long	0x7C7D1919
	.long	0x7C9E1919

	lwz	12,372(1)
	mtlr	0
	li	10,191
	li	11,207
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
	ld	27,376(1)
	ld	28,384(1)
	ld	29,392(1)
	ld	30,400(1)
	ld	31,408(1)
	addi	1,1,416
	blr	
.long	0
.byte	0,12,0x04,1,0x80,5,4,0
.long	0
.size	__poly1305_blocks_vsx,.-__poly1305_blocks_vsx

.align	6
.LPICmeup:
	mflr	0
	bcl	20,31,$+4
	mflr	12
	addi	12,12,56
	mtlr	0
	blr	
.long	0
.byte	0,12,0x14,0,0,0,0,0
.space	28

.long	0x03ffffff,0x00000000
.long	0x03ffffff,0x00000000
.long	0x0000001a,0x00000000
.long	0x0000001a,0x00000000
.long	0x00000028,0x00000000
.long	0x00000028,0x00000000
.long	0x0e0f0001,0x00000000
.long	0x1e1f1011,0x00000000
.long	0x01000000,0x01000000
.long	0x01000000,0x01000000
.long	0x03020100,0x07060504
.long	0x0b0a0908,0x0f0e0d0c

.long	0x00000000,0x00000000
.long	0x04050607,0x00000000
.long	0x00000000,0x04050607
.long	0x00000000,0x00000000
.long	0x00000000,0x00000000
.long	0x00000000,0x04050607

.long	0x00000000,0xffffffff
.long	0xffffffff,0xffffffff
.long	0x00000000,0xffffffff
.long	0x00000000,0xffffffff
.long	0x00000000,0x00000000
.long	0x00000000,0xffffffff
.byte	80,111,108,121,49,51,48,53,32,102,111,114,32,80,80,67,44,67,82,89,80,84,79,71,65,77,83,32,98,121,32,64,100,111,116,45,97,115,109,0
.align	2
