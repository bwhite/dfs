#include	<ctype.h>
#include	"chits.h"

static char	*charData;
static int	charDataLen;


static void XMLCALL
tag_start(void *data, const XML_Char *name, const XML_Char **atts)
{
    free(charData);
    charData = NULL;
}


static void chit_add_tag(chit_t *chit, int id, long val_l, char *val_s)
{
    attr_t	*a = malloc(sizeof(attr_t));
    a->tag = id;
    a->val_s = val_s;
    a->val_l = val_l;
    a->next = chit->attrs;
    chit->attrs = a;
}


static void XMLCALL
tag_end_chit(void *data, const XML_Char *el)
{
    chit_t 	*chit = (chit_t *)data;

    if (charData) {
	if (!strcmp(el, "fingerprint")) {
	    cry_ascii_to_hash(chit->fingerprint, charData);
	    free(charData);

	} else if (!strcmp(el, "serverprint")) {
	    cry_ascii_to_hash(chit->serverprint, charData);
	    free(charData);

	} else if (!strcmp(el, "server")) {
	    chit->server = charData;

	} else if (!strcmp(el, "id")) {
	    chit->id = strtoll(charData, NULL, 10);
	    free(charData);

	} else if (!strcmp(el, "version")) {
	    chit->version = strtol(charData, NULL, 10);
	    free(charData);

	} else if (!strcmp(el, "narrow")) {
	    chit_add_tag(chit, TAG_NARROW, 0, charData);

	} else if (!strcmp(el, "remove")) {
	    if (tolower(charData[0]) == 'r')
		chit_add_tag(chit, TAG_REMOVE_RIGHT, RIGHT_READ, NULL);
	    else if (tolower(charData[0]) == 'w')
		chit_add_tag(chit, TAG_REMOVE_RIGHT, RIGHT_WRITE, NULL);
	    else if (tolower(charData[0]) == 'd')
		chit_add_tag(chit, TAG_REMOVE_RIGHT, RIGHT_DELETE, NULL);
	    else if (tolower(charData[0]) == 'c')
		chit_add_tag(chit, TAG_REMOVE_RIGHT, RIGHT_CREATE, NULL);

	} else if (!strcmp(el, "public")) {
	    chit_add_tag(chit, TAG_PUBLIC_KEY, 0, charData);

	} else if (!strcmp(el, "label")) {
	    chit_add_tag(chit, TAG_LABEL, 0, charData);

	} else if (!strcmp(el, "chit") ||!strcmp(el, "tags")) {
	    free(charData);

	} else {
	    dfs_out("Bad tag '%s'\n", el);
	}
    }
    charData = NULL;
}



static void XMLCALL
xml_char(void *data, const XML_Char *s, int len)
{
    if (!charData) {
	charData = (char *)malloc(len + 1);
	charDataLen = len + 1;
	strncpy(charData, s, len);
	charData[len] = 0;
    } else {
	char *tmp = (char *)malloc(charDataLen + len);
	strcpy(tmp, charData);
	strncat(tmp, s, len);
	tmp[charDataLen + len - 1] = 0;
	free(charData);
	charData = tmp;
	charDataLen += len;
    }
}


static XML_Parser reset_parser(XML_EndElementHandler tag_end, void *ptr) {
    static XML_Parser parser;

    if (!parser) {
	parser = XML_ParserCreate(NULL);
    } else {
	XML_ParserReset(parser, NULL);
    }
    XML_SetElementHandler(parser, tag_start, tag_end);
    XML_SetCharacterDataHandler(parser, xml_char);
    XML_SetUserData(parser, ptr);
    return parser;
}


// Allocs a new chit_t, frees the xcred
chit_t *xcred_parse(char *xcred) 
{
    chit_t	*chit;
    XML_Parser 	parser;

    if (!xcred) return NULL;

    chit = (chit_t *)calloc(1, sizeof(chit_t));
    parser = reset_parser(tag_end_chit, chit);

    if (XML_Parse(parser, xcred, strlen(xcred), 0) == XML_STATUS_ERROR) {
	dfs_out("Parse error at line %d:\n%s\n",
		XML_GetCurrentLineNumber(parser),
		XML_ErrorString(XML_GetErrorCode(parser)));
	return NULL;
    }

    if (!chit->id) {
	chit_free(chit);
	return NULL;
    }
    free(xcred);

    // reverse the attributes
    attr_t	*c, *l = NULL;
    chit->attrs_last = chit->attrs;
    while ((c = chit->attrs)) {
	chit->attrs = c->next;
	c->next = l;
	l = c;
    }
    chit->attrs = l;

    return chit;
}


chit_t *chit_read(char *fname)
{
    return xcred_parse(read_text_file(fname));
}


void chit_save(chit_t *chit, char *outfile) 
{
    FILE	*fp;
    attr_t	*a;

    assert(chit);

    if (!(fp = fopen(outfile, "w"))) {
	dfs_out("No open chit output '%s'\n", outfile);
	return;
    }

    fprintf(fp, "<chit>\n");
    fprintf(fp, "\t<server>%s</server>\n", chit->server);
    
    char *s = cry_hash_to_ascii(chit->serverprint);
    fprintf(fp, "\t<serverprint>%s</serverprint>\n", s);
    free(s);

    s = cry_hash_to_ascii(chit->fingerprint);
    fprintf(fp, "\t<fingerprint>%s</fingerprint>\n", s);
    free(s);

    fprintf(fp, "\t<id>%ld</id>\n", (long)chit->id);
    fprintf(fp, "\t<version>%ld</version>\n", (long)chit->version);

    fprintf(fp, "\t<tags>\n");
    for (a = chit->attrs; a; a = a->next) 
	fprintf(fp, "\t\t<%s>%s</%s>\n", rightsTags[a->tag], a->val_l ? rightsTags[a->val_l] : a->val_s, rightsTags[a->tag]);
    fprintf(fp, "\t</tags>\n</chit>\n");
    fclose(fp);
}

