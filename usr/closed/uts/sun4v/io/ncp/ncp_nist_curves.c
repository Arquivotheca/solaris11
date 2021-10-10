/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Do not delete this notice.  It is here at the request of Sun legal.
 * Sun does not support and never will support any the following:
 *
 * The ECMQV or other MQV related algorithms/mechanisms.
 *
 * Point compression and uncompression (representing a point <x,y>
 * using only the x-coordinate and one extra bit  <x, LSB(y)>).
 *
 * Normal basis representation of polynomials (X^1, x^2, x^4, x^8...).
 *
 * Validation of arbitrary curves.
 *
 * ===
 * If you are a developer, and thinking about implementing any of these,
 * stop.  Don't do it.  If you have any questions, call Sheueling Chang.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>

#include "ncp_ecc.h"

#define	NUM_CURVES	ECC_NUM_CURVES
#define	MAX_ECC_OIDLEN	16

#define	CHECK(expr) if ((rv = (expr)) != BIG_OK) { goto cleanexit; }
#define	CONVERTRV (rv >= 0 ? rv : bigerrcode_to_crypto_errcode(rv))


typedef struct {
	char			*name;
	uchar_t			OID[MAX_ECC_OIDLEN];
	int			OIDlen;
	int			flags;
	int			degree;
	char			*modulus;
	int32_t			abinary;
	char			*b;
	char			*bpx;
	char			*bpy;
	char			*order;
	int			cofactor;
} ECC_raw_curve_t;

ECC_curve_t ncp_ECC_curves[NUM_CURVES] = {{{0, }, NULL, }, };

static ECC_raw_curve_t raw_curve_data[NUM_CURVES] = {

	{
		"P-192", /* name */
		{
			0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03,
			0x01, 0x01
		}, /* OID */
		10, /* OID length */
		0, /* flags */
		192, /* degree */
		"000000000fffffffffffffffffffffffffffffffeff"
		"ffffffffffffff", /* modulus */
		(int32_t)(-3), /* abinary */
		"064210519E59C80e70fa7e9ab72243049feb8deecc146b9b1", /* b */
		"0188DA80EB03090F67CBF20EB43A18800F4FF0AFD82FF1012", /* bpx */
		"007192B95FFC8DA78631011ED6B24CDD573F977A11E794811", /* bpy */
		"0ffffffffffffffffffffffff99def836146bc9b1b4d22831", /* order */
		1  /* cofactor */
	},
	{
		"P-224", /* name */
		{ 0x06, 0x05, 0x2b, 0x81, 0x04, 0x00, 0x21 }, /* OID */
		7, /* OID length */
		0, /* flags */
		224, /* degree */
		"000000000ffffffffffffffffffffffffffffffff00"
		"0000000000000000000001", /* modulus */
		(int32_t)(-3), /* abinary */
		"0b4050a850c04b3abf54132565044b0b7d7bfd8ba27"
		"0b39432355ffb4", /* b */
		"0b70e0cbd6bb4bf7f321390b94a03c1d356c2112234"
		"3280d6115c1d21", /* bpx */
		"0bd376388b5f723fb4c22dfe6cd4375a05a07476444"
		"d5819985007e34", /* bpy */
		"0ffffffffffffffffffffffffffff16a2e0b8f03e13"
		"dd29455c5c2a3d", /* order */
		1  /* cofactor */
	},
	{
		"P-256", /* name */
		{
			0x06, 0x08, 0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03,
			0x01, 0x07
		}, /* OID */
		10, /* OID length */
		0, /* flags */
		256, /* degree */
		"000000000ffffffff00000001000000000000000000"
		"000000ffffffffffffffffffffffff", /* modulus */
		(int32_t)(-3), /* abinary */
		"05ac635d8aa3a93e7b3ebbd55769886bc651d06b0cc"
		"53b0f63bce3c3e27d2604b", /* b */
		"06b17d1f2e12c4247f8bce6e563a440f277037d812d"
		"eb33a0f4a13945d898c296", /* bpx */
		"04fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b"
		"315ececbb6406837bf51f5", /* bpy */
		"0ffffffff00000000ffffffffffffffffbce6faada7"
		"179e84f3b9cac2fc632551", /* order */
		1  /* cofactor */
	},
	{
		"P-384", /* name */
		{ 0x06, 0x05, 0x2b, 0x81, 0x04, 0x00, 0x22 }, /* OID */
		7, /* OID length */
		0, /* flags */
		384, /* degree */
		"000000000ffffffffffffffffffffffffffffffffff"
		"fffffffffffffffffffffffffffffeffffffff00000"
		"00000000000ffffffff", /* modulus */
		(int32_t)(-3), /* abinary */
		"0b3312fa7e23ee7e4988e056be3f82d19181d9c6efe"
		"8141120314088f5013875ac656398d8a2ed19d2a85c"
		"8edd3ec2aef", /* b */
		"0aa87ca22be8b05378eb1c71ef320ad746e1d3b628b"
		"a79b9859f741e082542a385502f25dbf55296c3a545"
		"e3872760ab7", /* bpx */
		"03617de4a96262c6f5d9e98bf9292dc29f8f41dbd28"
		"9a147ce9da3113b5f0b8c00a60b1ce1d7e819d7a431"
		"d7c90ea0e5f", /* bpy */
		"0ffffffffffffffffffffffffffffffffffffffffff"
		"ffffffc7634d81f4372ddf581a0db248b0a77aecec1"
		"96accc52973", /* order */
		1  /* cofactor */
	},
	{
		"P-521", /* name */
		{ 0x06, 0x05, 0x2b, 0x81, 0x04, 0x00, 0x23 }, /* OID */
		7, /* OID length */
		0, /* flags */
		521, /* degree */
		"000000001ffffffffffffffffffffffffffffffffff"
		"fffffffffffffffffffffffffffffffffffffffffff"
		"fffffffffffffffffffffffffffffffffffffffffff"
		"ffffffffff", /* modulus */
		(int32_t)(-3), /* abinary */
		"051953eb9618e1c9a1f929a21a0b68540eea2da725b"
		"99b315f3b8b489918ef109e156193951ec7e937b165"
		"2c0bd3bb1bf073573df883d2c34f1ef451fd46b503f"
		"00", /* b */
		"0c6858e06b70404e9cd9e3ecb662395b4429c648139"
		"053fb521f828af606b4d3dbaa14b5e77efe75928fe1"
		"dc127a2ffa8de3348b3c1856a429bf97e7e31c2e5bd"
		"66", /* bpx */
		"11839296a789a3bc0045c8a5fb42c7d1bd998f54449"
		"579b446817afbd17273e662c97ee72995ef42640c55"
		"0b9013fad0761353c7086a272c24088be94769fd166"
		"50", /* bpy */
		"1ffffffffffffffffffffffffffffffffffffffffff"
		"fffffffffffffffffffffffa51868783bf2f966b7fc"
		"c0148f709a5d03bb5c9b8899c47aebb6fb71e913864"
		"09", /* order */
		1  /* cofactor */
	},
	{
		"B-163", /* name */
		{ 0x06, 0x05, 0x2b, 0x81, 0x04, 0x00, 0x0f }, /* OID */
		7, /* OID length */
		ECC_POLY, /* flags */
		163, /* degree */
		"800000000000000000000000000000000000000c9", /* modulus */
		1, /* abinary */
		"20a601907b8c953ca1481eb10512f78744a3205fd", /* b */
		"3f0eba16286a2d57ea0991168d4994637e8343e36", /* bpx */
		"d51fbc6c71a0094fa2cdd545b11c5c0c797324f1", /* bpy */
		"40000000000000000000292fe77e70c12a4234c33", /* order */
		2  /* cofactor */
	},
	{
		"B-233", /* name */
		{ 0x06, 0x05, 0x2b, 0x81, 0x04, 0x00, 0x1b }, /* OID */
		7, /* OID length */
		ECC_POLY, /* flags */
		233, /* degree */
		"2000000000000000000000000000000000000000400"
		"0000000000000001", /* modulus */
		1, /* abinary */
		"66647ede6c332c7f8c0923bb58213b333b20e9ce428"
		"1fe115f7d8f90ad", /* b */
		"fac9dfcbac8313bb2139f1bb755fef65bc391f8b36f"
		"8f8eb7371fd558b", /* bpx */
		"1006a08a41903350678e58528bebf8a0beff867a7ca"
		"36716f7e01f81052", /* bpy */
		"1000000000000000000000000000013e974e72f8a69"
		"22031d2603cfe0d7", /* order */
		2  /* cofactor */
	},
	{
		"B-283", /* name */
		{ 0x06, 0x05, 0x2b, 0x81, 0x04, 0x00, 0x11 }, /* OID */
		7, /* OID length */
		ECC_POLY, /* flags */
		283, /* degree */
		"8000000000000000000000000000000000000000000"
		"00000000000000000000000010a1", /* modulus */
		1, /* abinary */
		"27b680ac8b8596da5a4af8a19a0303fca97fd764530"
		"9fa2a581485af6263e313b79a2f5", /* b */
		"5f939258db7dd90e1934f8c70b0dfec2eed25b8557e"
		"ac9c80e2e198f8cdbecd86b12053", /* bpx */
		"3676854fe24141cb98fe6d4b20d02b4516ff702350e"
		"ddb0826779c813f0df45be8112f4", /* bpy */
		"3ffffffffffffffffffffffffffffffffffef903996"
		"60fc938a90165b042a7cefadb307", /* order */
		2  /* cofactor */
	},
	{
		"B-409", /* name */
		{ 0x06, 0x05, 0x2b, 0x81, 0x04, 0x00, 0x25 }, /* OID */
		7, /* OID length */
		ECC_POLY, /* flags */
		409, /* degree */
		"2000000000000000000000000000000000000000000"
		"0000000000000000000000000000000000000080000"
		"00000000000000001", /* modulus */
		1, /* abinary */
		"21a5c2c8ee9feb5c4b9a753b7b476b7fd6422ef1f3d"
		"d674761fa99d6ac27c8a9a197b272822f6cd57a55aa"
		"4f50ae317b13545f", /* b */
		"15d4860d088ddb3496b0c6064756260441cde4af177"
		"1d4db01ffe5b34e59703dc255a868a1180515603aea"
		"b60794e54bb7996a7", /* bpx */
		"61b1cfab6be5f32bbfa78324ed106a7636b9c5a7bd1"
		"98d0158aa4f5488d08f38514f1fdf4b4f40d2181b36"
		"81c364ba0273c706", /* bpy */
		"1000000000000000000000000000000000000000000"
		"0000000001e2aad6a612f33307be5fa47c3c9e052f8"
		"38164cd37d9a21173", /* order */
		2  /* cofactor */
	},
	{
		"B-571", /* name */
		{ 0x06, 0x05, 0x2b, 0x81, 0x04, 0x00, 0x27 }, /* OID */
		7, /* OID length */
		ECC_POLY, /* flags */
		571, /* degree */
		"8000000000000000000000000000000000000000000"
		"0000000000000000000000000000000000000000000"
		"0000000000000000000000000000000000000000000"
		"00000000000425", /* modulus */
		1, /* abinary */
		"2f40e7e2221f295de297117b7f3d62f5c6a97ffcb8c"
		"eff1cd6ba8ce4a9a18ad84ffabbd8efa59332be7ad6"
		"756a66e294afd185a78ff12aa520e4de739baca0c7f"
		"feff7f2955727a", /* b */
		"303001d34b856296c16c0d40d3cd7750a93d1d2955f"
		"a80aa5f40fc8db7b2abdbde53950f4c0d293cdd711a"
		"35b67fb1499ae60038614f1394abfa3b4c850d927e1"
		"e7769c8eec2d19", /* bpx */
		"37bf27342da639b6dccfffeb73d69d78c6c27a6009c"
		"bbca1980f8533921e8a684423e43bab08a576291af8"
		"f461bb2a8b3531d2f0485c19b16e2f1516e23dd3c1a"
		"4827af1b8ac15b", /* bpy */
		"3ffffffffffffffffffffffffffffffffffffffffff"
		"ffffffffffffffffffffffffffffe661ce18ff55987"
		"308059b186823851ec7dd9ca1161de93d5174d66e83"
		"82e9bb2fe84e47", /* order */
		2  /* cofactor */
	},
	{
		"K-163", /* name */
		{ 0x06, 0x05, 0x2b, 0x81, 0x04, 0x00, 0x01 }, /* OID */
		7, /* OID length */
		ECC_POLY, /* flags */
		163, /* degree */
		"800000000000000000000000000000000000000c9", /* modulus */
		1, /* abinary */
		"01", /* b */
		"02fe13c0537bbc11acaa07d793de4e6d5e5c94eee8", /* bpx */
		"0289070fb05d38ff58321f2e800536d538ccdaa3d9", /* bpy */
		"04000000000000000000020108a2e0cc0d99f8a5ef", /* order */
		2  /* cofactor */
	},
	{
		"K-233", /* name */
		{ 0x06, 0x05, 0x2b, 0x81, 0x04, 0x00, 0x1a }, /* OID */
		7, /* OID length */
		ECC_POLY, /* flags */
		233, /* degree */
		"2000000000000000000000000000000000000000400"
		"0000000000000001", /* modulus */
		0, /* abinary */
		"01", /* b */
		"017232ba853a7e731af129f22ff4149563a419c26bf5"
		"0a4c9d6eefad6126", /* bpx */
		"01db537dece819b7f70f555a67c427a8cd9bf18aeb9b"
		"56e0c11056fae6a3", /* bpy */
		"8000000000000000000000000000069d5bb915bcd4"
		"6efb1ad5f173abdf", /* order */
		4  /* cofactor */
	},
	{
		"K-283", /* name */
		{ 0x06, 0x05, 0x2b, 0x81, 0x04, 0x00, 0x10 }, /* OID */
		7, /* OID length */
		ECC_POLY, /* flags */
		283, /* degree */
		"8000000000000000000000000000000000000000000"
		"00000000000000000000000010a1", /* modulus */
		0, /* abinary */
		"01", /* b */
		"0503213f78ca44883f1a3b8162f188e553cd265f23c1567a"
		"16876913b0c2ac2458492836", /* bpx */
		"01ccda380f1c9e318d90f95d07e5426fe87e45c0e8184698"
		"e45962364e34116177dd2259", /* bpy */
		"01ffffffffffffffffffffffffffffffffffe9ae2ed07577"
		"265dff7f94451e061e163c61", /* order */
		4  /* cofactor */
	},
	{
		"K-409", /* name */
		{ 0x06, 0x05, 0x2b, 0x81, 0x04, 0x00, 0x24 }, /* OID */
		7, /* OID length */
		ECC_POLY, /* flags */
		409, /* degree */
		"2000000000000000000000000000000000000000000"
		"0000000000000000000000000000000000000080000"
		"00000000000000001", /* modulus */
		0, /* abinary */
		"01", /* b */
		"0060f05f658f49c1ad3ab1890f7184210efd0987e307c84c"
		"27accfb8f9f67cc2c460189eb5aaaa62ee222eb1b35540cf"
		"e9023746", /* bpx */
		"01e369050b7c4e42acba1dacbf04299c3460782f918ea427"
		"e6325165e9ea10e3da5f6c42e9c55215aa9ca27a5863ec48"
		"d8e0286b", /* bpy */
		"7fffffffffffffffffffffffffffffffffffffffffffff"
		"fffffe5f83b2d4ea20400ec4557d5ed3e3e7ca5b4b5c83b8"
		"e01e5fcf", /* order */
		2  /* cofactor */
	},
	{
		"K-571", /* name */
		{ 0x06, 0x05, 0x2b, 0x81, 0x04, 0x00, 0x26 }, /* OID */
		7, /* OID length */
		ECC_POLY, /* flags */
		571, /* degree */
		"8000000000000000000000000000000000000000000"
		"0000000000000000000000000000000000000000000"
		"0000000000000000000000000000000000000000000"
		"00000000000425", /* modulus */
		0, /* abinary */
		"01", /* b */
		"026eb7a859923fbc82189631f8103fe4ac9ca297"
		"0012d5d46024804801841ca44370958493b205e6"
		"47da304db4ceb08cbbd1ba39494776fb988b4717"
		"4dca88c7e2945283a01c8972", /* bpx */
		"0349dc807f4fbf374f4aeade3bca95314dd58cec"
		"9f307a54ffc61efc006d8a2c9d4979c0ac44aea7"
		"4fbebbb9f772aedcb620b01a7ba7af1b320430c8"
		"591984f601cd4c143ef1c7a3", /* bpy */
		"0200000000000000000000000000000000000000"
		"00000000000000000000000000000000131850e1"
		"f19a63e4b391a8db917f4138b630d84be5d63938"
		"1e91deb45cfe778f637c1001", /* order */
		4  /* cofactor */
	}
};


/*
 * Converts a hex string to a kcl (giant big-endian) value. resultlen
 * is the size of the result buffer.  *actuallen is set to the length
 * of the number placed there, in bytes.
 */
static BIG_ERR_CODE
hex_to_kcl(uint8_t *result, int *actuallen, int resultlen, char *arg)
{
	int		i;  /* index into string */
	int		arglen = strlen(arg);
	int		j = arglen & 1;  /* index into result, in nibbles */
	uint8_t		tmp = 0;
	char		c;

	for (i = 0; i < arglen; ++i) {
		c = arg[i];
		if ('0' <= c && c <= '9') {
			c = c - '0';
		} else if ('a' <= c && c <= 'f') {
			c = c - 'a' + 10;
		} else if ('A' <= c && c <= 'F') {
			c = c - 'A' + 10;
		} else {
			return (BIG_INVALID_ARGS);
		}

		tmp = tmp << 4 | c;
		++j;

		if ((j & 1) == 0) {
			/* We now have the the byte fully formed */
			if (j / 2 > resultlen) {
				return (BIG_BUFFER_TOO_SMALL);
			}
			result[j / 2 - 1] = tmp;
			tmp = 0;
		}
	}

	*actuallen = j / 2;

	return (BIG_OK);
}


static BIG_ERR_CODE
hex_to_bignum(BIGNUM *result, char *arg)
{
	int		arglen = strlen(arg);
	int		i;  /* index into string */
	int		j = 0;  /* index into number in bits */
	int		c;
	int		rv = BIG_OK;

	CHECK(ncp_big_extend(result,
	    (arglen + (BIG_CHUNK_SIZE / 4) - 1) / (BIG_CHUNK_SIZE / 4) + 1));
	result->sign = 1;
	for (i = 0; i < result->size; ++i) {
		result->value[i] = 0;
	}
	for (i = 0; i < arglen; ++i) {
		c = arg[arglen - 1 - i];
		if ('0' <= c && c <= '9') {
			c = c - '0';
		} else if ('a' <= c && c <= 'f') {
			c = c - 'a' + 10;
		} else if ('A' <= c && c <= 'F') {
			c = c - 'A' + 10;
		} else {
			return (BIG_INVALID_ARGS);
		}
		/* assert(j / BIG_CHUNK_SIZE < result->size); */
		result->value[j / BIG_CHUNK_SIZE] |=
		    (BIG_CHUNK_TYPE)c << (j % BIG_CHUNK_SIZE);
		j += 4;
	}
	result->len = j / BIG_CHUNK_SIZE + 1;
	while ((result->len > 0) && (result->value[result->len - 1] == 0)) {
		result->len--;
	}

cleanexit:

	return (rv);
}


#define	KCLBUFSIZE (1024 / BITSINBYTE)

int
ncp_ECC_build_curve_table(void)
{
	int		i;
	int		rv;
	int		len;
	uint8_t		kclbuf[KCLBUFSIZE];
	BIGNUM		x;
	BIGNUM		y;

	x.malloced = 0;
	y.malloced = 0;

	if (ncp_ECC_curves[0].name[0] != '\0') {
		return (0);
	}

	(void) memset(ncp_ECC_curves, 0, sizeof (ncp_ECC_curves));
	CHECK(ncp_big_init(&x, 1024 / BIG_CHUNK_SIZE));
	CHECK(ncp_big_init(&y, 1024 / BIG_CHUNK_SIZE));

	for (i = 0; i < NUM_CURVES; ++i) {
		ECC_curve_t	*p = &ncp_ECC_curves[i];
		ECC_raw_curve_t *r = & raw_curve_data[i];

		(void) strncpy(p->name, r->name, sizeof (p->name));
		p->OID = (char *)(r->OID);
		p->OIDlen = r->OIDlen;
		p->modulusinfo.flags = r->flags;
		CHECK(ncp_big_init(&p->modulusinfo.modulus,
		    r->degree / BIG_CHUNK_SIZE + 3));
		CHECK(hex_to_bignum(&p->modulusinfo.modulus, r->modulus));
		CHECK(ECC_fluff_modulus(&p->modulusinfo));
		if (p->modulusinfo.modulusMSB != r->degree -
		    (p->modulusinfo.flags & ECC_POLY ? 0 : 1)) { /* check */
			rv = BIG_TEST_FAILED;
			goto cleanexit;
		}
		p->abinary = r->abinary;
		CHECK(ncp_big_init(&p->a,
		    p->modulusinfo.modulusMSB / BIG_CHUNK_SIZE + 3));
		CHECK(ncp_big_set_int(&p->a, r->abinary));
		if (p->abinary < 0) {
			if (p->modulusinfo.flags & ECC_POLY) {
				rv = BIG_INVALID_ARGS;
				goto cleanexit;
			}
			CHECK(ncp_big_add(&p->a, &p->a,
			    &p->modulusinfo.modulus));
		}

		CHECK(ncp_big_mont_encode(&p->a, &p->a,
		    p->modulusinfo.flags & ECC_POLY, &p->modulusinfo.modulus,
		    p->modulusinfo.nprime, &p->modulusinfo.R));
		CHECK(hex_to_kcl(kclbuf, &len, KCLBUFSIZE, r->b));
		CHECK(ncp_big_init(&p->b,
		    p->modulusinfo.modulusMSB / BIG_CHUNK_SIZE + 3));
		CHECK(ncp_kcl_to_bignum(&p->b, kclbuf, len,
		    1, /* check */
		    1, /* mont */
		    p->modulusinfo.flags & ECC_POLY,
		    &p->modulusinfo.modulus,
		    p->modulusinfo.modulusMSB,
		    p->modulusinfo.nprime,
		    &p->modulusinfo.R));
		CHECK(ncp_big_init(&p->order,
		    p->modulusinfo.modulusMSB / BIG_CHUNK_SIZE + 3));
		CHECK(hex_to_bignum(&p->order, r->order));
		p->orderMSB = ncp_big_MSB(&p->order);
		p->cofactor = r->cofactor;
		CHECK(ECC_point_init(&p->basepoint, p));
		CHECK(hex_to_bignum(&x, r->bpx));
		CHECK(hex_to_bignum(&y, r->bpy));
		CHECK(ECC_point_set(&p->basepoint, &x, &y, p));
		CHECK(ECC_point_in_curve(&p->basepoint, p)); /* a check */
#if 0
		CHECK(ECC_set_curve_immutable(p));
#else
		CHECK(set_AZ4(&p->basepoint, p));
#endif
	}


cleanexit:

	ncp_big_finish(&x);
	ncp_big_finish(&y);
	if (rv != BIG_OK) {
		ncp_ECC_destroy_curve_table();
	}

	return (CONVERTRV);
}

void
ncp_ECC_destroy_curve_table(void)
{
	int	i;

	for (i = 0; i < NUM_CURVES; ++i) {
		ECC_curve_t	*p = &ncp_ECC_curves[i];

		ECC_point_finish(&p->basepoint);
		ncp_big_finish(&p->a);
		ncp_big_finish(&p->b);
		ncp_big_finish(&p->order);
		ncp_big_finish(&p->modulusinfo.R2);
		ncp_big_finish(&p->modulusinfo.Rinv);
		ncp_big_finish(&p->modulusinfo.One);
		ncp_big_finish(&p->modulusinfo.R);
		ncp_big_finish(&p->modulusinfo.modulus);
	}
}


ECC_curve_t *
ncp_ECC_find_curve(uint8_t *OID)
{
	int i;

	for (i = 0; i < NUM_CURVES; ++i) {
		if (memcmp(ncp_ECC_curves[i].OID, OID,
		    ncp_ECC_curves[i].OIDlen) == 0) {
			return (&ncp_ECC_curves[i]);
		}
	}

	return (NULL);
}
