
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <assert.h>
#include <gcrypt.h>
#include "cry.h"

static char *privateKey = "(private-key "
" (rsa "
"  (n #00BA0F142987874A10845ED79DAAD4EF13CF426524D5FDC97CDA341CF26630AFF19628E6ACA783EC753E106930E2A24835A3928C16A78F52A50CECF25B26F8D5D98BCE95B32A3A7F519587439DAD5E8CB68B69C6377AEBD16B69A10D9CC128248CAA875BAEE4753EB54DE602D629C094BA7E8A8C193DDFFEE5EFFDCFD4DA0868DF#)"
"  (e #010001#)"
"  (d #3A706F044649D39511869A91AEAD17F4B06629CFA2990676BD309E20F2C0FB1A55F03DD7DFFBBB42EDFE08942EB30B1C31E6DF326469CB6B04FECD769CEC8E7A2F627F639806291023D4CF4E6EA2834BF82A8BEEF2DC05C1366CBB657310CA83FC12317A01A449BCB7BB1B0AE0CF36F734D3B10200BFA17FF3B4EE34FB5B2531#)"
"  (p #00D5A2F805B7C2FAFDB2503E134BA358D16B20507BCBE9EF28593627E5DB9233FFEE36419CBF68579D7E3E4A62667B8C94928943BA90E6F73EA0B11449F5CB3951#)"
"  (q #00DEF428717DDC9E627D478920EBAD95E6C3119336CA103BF38E1A196DF693C6516B1F64738BD74A4FC9D64926CD132074D267D9524F811852DE8A0095C901F32F#)"
"  (u #078B659A8586B9A8F90DED0CF83DA29B862EBB198426AE9320349DF4E03840E42F18349A562B078DC267905528C25B98BE35AF96CC9BC7BA20FC228E1CD7BAD1#)"
"  )"
    " )";


static char *publicKey = "(public-key "
" (rsa "
"  (n #00BA0F142987874A10845ED79DAAD4EF13CF426524D5FDC97CDA341CF26630AFF19628E6ACA783EC753E106930E2A24835A3928C16A78F52A50CECF25B26F8D5D98BCE95B32A3A7F519587439DAD5E8CB68B69C6377AEBD16B69A10D9CC128248CAA875BAEE4753EB54DE602D629C094BA7E8A8C193DDFFEE5EFFDCFD4DA0868DF#)"
"  (e #010001#)"
"  )"
    " )";


char		*sessionKey;		// not used


int main(int argc, char **argv)
{
    char	*input = "hello there this is, is a random length string";	// 46 + 1 bytes;
    char	*input2 = "this is shorted";
    char	*sessionKey = "012345678912345";
    char	*output;
    char	*output2;
    char	*output3;
    char	*output4;
    size_t	sz, sz2;
    int		arr[] = { 42, 53, 28, 53, 53, 53, 42, 53, 42, 53, 42, 53, 42, 53, 42, 53, 42, 53, 42, 53, 42, 53, 42, 53, 42, 53, 42, 53};

    cry_asym_init();

    cry_sym_init(sessionKey);
    cry_sym_encrypt(&output, &sz, input, strlen(input) + 1);
    cry_sym_encrypt(&output2, &sz2, input2, strlen(input2) + 1);

    cry_sym_init(sessionKey);		// re-init to get the fresh IV back
    cry_sym_decrypt(&output3, &sz, output, sz);
    cry_sym_decrypt(&output4, &sz2, output2, sz2);
    printf("Symmetric: %s / '%s' (%d)\n", input, output3, (int)sz);
    printf("Symmetric: %s / '%s' (%d)\n", input2, output4, (int)sz2);

    cry_asym_encrypt(&output, &sz, input, strlen(input) + 1, publicKey);
    cry_asym_decrypt(&output2, &sz2, output, sz, privateKey);
    printf("PUBLIC KEY '%s' / '%s'\n", input, output2);

    cry_asym_encrypt(&output, &sz, (char *)arr, sizeof(arr), publicKey);
    cry_asym_decrypt(&output2, &sz2, output, sz, privateKey);
    int *o = (int *)output2;
    printf("PUBLIC arr %d, %d, %d (%d bytes (was %ld))\n", 
	   ((int *)((char*)o + sz2))[-3], 
	   ((int *)((char*)o + sz2))[-2], 
	   ((int *)((char*)o + sz2))[-1],
	   (int)sz2, sizeof(arr));


    return 0;
}

