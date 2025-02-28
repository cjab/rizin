// SPDX-FileCopyrightText: 2009-2020 pancake <pancake@nopcode.org>
// SPDX-License-Identifier: LGPL-3.0-only

#include <stddef.h>
#include <math.h> // required for signbit
#include "rz_cons.h"
#include "rz_core.h"
#include "rz_util.h"

static ut32 vernum(const char *s) {
	// XXX this is known to be buggy, only works for strings like "x.x.x"
	// XXX anything like "x.xx.x" will break the parsing
	// XXX -git is ignored, maybe we should shift for it
	char *a = strdup(s);
	a = rz_str_replace(a, ".", "0", 1);
	char *dash = strchr(a, '-');
	if (dash) {
		*dash = 0;
	}
	ut32 res = atoi(a);
	free(a);
	return res;
}

static const char *help_msg_percent[] = {
	"Usage:", "%[name[=value]]", "Set each NAME to VALUE in the environment",
	"%", "", "list all environment variables",
	"%", "SHELL", "prints SHELL value",
	"%", "TMPDIR=/tmp", "sets TMPDIR value to \"/tmp\"",
	NULL
};

// NOTE: probably not all environment vars takes sesnse
// because they can be replaced by commands in the given
// command.. we should only expose the most essential and
// unidirectional ones.
static const char *help_msg_env[] = {
	"\nEnvironment:", "", "",
	"RZ_FILE", "", "file name",
	"RZ_OFFSET", "", "10base offset 64bit value",
	"RZ_XOFFSET", "", "same as above, but in 16 base",
	"RZ_BSIZE", "", "block size",
	"RZ_ENDIAN", "", "'big' or 'little'",
	"RZ_IOVA", "", "is io.va true? virtual addressing (1,0)",
	"RZ_DEBUG", "", "debug mode enabled? (1,0)",
	"RZ_SIZE", "", "file size",
	"RZ_ARCH", "", "value of asm.arch",
	"RZ_BITS", "", "arch reg size (8, 16, 32, 64)",
	"RZ_BIN_LANG", "", "assume this lang to demangle",
	"RZ_BIN_DEMANGLE", "", "demangle or not",
	"RZ_BIN_PDBSERVER", "", "e pdb.server",
	NULL
};

static const char *help_msg_exclamation[] = {
	"Usage:", "!<cmd>", "  Run given command as in system(3)",
	"!", "", "list all historic commands",
	"!*", "rzp x", "run rizin command via rzpipe in current session",
	"!!", "", "save command history to hist file",
	"!!", "ls~txt", "print output of 'ls' and grep for 'txt'",
	"!!!", "cmd [args|$type]", "adds the autocomplete value",
	"!!!-", "cmd [args]", "removes the autocomplete value",
	".!", "rz_bin -rpsei ${FILE}", "run each output line as a rizin cmd",
	"!", "echo $RZ_SIZE", "display file size",
	"!-", "", "clear history in current session",
	"!-*", "", "clear and save empty history log",
	"R=!", "", "enable remotecmd mode",
	"R!=", "", "disable remotecmd mode",
	NULL
};

static const char *help_msg_question_e[] = {
	"Usage: ?e[=bdgnpst] arg", "print/echo things", "",
	"?e", "", "echo message with newline",
	"?e=", " 32", "progress bar at 32 percentage",
	"?eg", " 10 20", "move cursor to column 10, row 20",
	"?en", " nonl", "echo message without ending newline",
	"?et", " msg", "change terminal title",
	NULL
};

static const char *help_msg_question[] = {
	"Usage: ?[?[?]] expression", "", "",
	"?!", " [cmd]", "run cmd if $? == 0",
	"?", " eip-0x804800", "show all representation result for this math expr",
	"?$", "", "show value all the variables ($)",
	"?+", " [cmd]", "run cmd if $? > 0",
	"?-", " [cmd]", "run cmd if $? < 0",
	"?=", " eip-0x804800", "hex and dec result for this math expr",
	"?==", " x86 `e asm.arch`", "strcmp two strings",
	"??", " [cmd]", "run cmd if $? != 0",
	"??", "", "show value of operation",
	"?a", "", "show ascii table",
	"?B", " [elem]", "show range boundaries like 'e?search.in",
	"?b", " [num]", "show binary value of number",
	"?b64[-]", " [str]", "encode/decode in base64",
	"?btw", " num|expr num|expr num|expr", "returns boolean value of a <= b <= c",
	"?e", "[=bdgnpst] arg", "echo messages, bars, pie charts and more (see ?e? for details)",
	"?f", " [num] [str]", "map each bit of the number as flag string index",
	"?F", "", "flush cons output",
	"?h", " [str]", "calculate hash for given string",
	"?i", "[ynmkp] arg", "prompt for number or Yes,No,Msg,Key,Path and store in $$?",
	"?ik", "", "press any key input dialog",
	"?im", " message", "show message centered in screen",
	"?in", " prompt", "noyes input prompt",
	"?ip", " prompt", "path input prompt",
	"?iy", " prompt", "yesno input prompt",
	"?j", " arg", "same as '? num' but in JSON",
	"?l", "[q] str", "returns the length of string ('q' for quiet, just set $?)",
	"?o", " num", "get octal value",
	"?P", " paddr", "get virtual address for given physical one",
	"?p", " vaddr", "get physical address for given virtual address",
	"?q", " eip-0x804800", "compute expression like ? or ?v but in quiet mode",
	"?r", " [from] [to]", "generate random number between from-to",
	"?s", " from to step", "sequence of numbers from to by steps",
	"?t", " cmd", "returns the time to run a command",
	"?T", "", "show loading times",
	"?u", " num", "get value in human units (KB, MB, GB, TB)",
	"?v", " eip-0x804800", "show hex value of math expr",
	"?vi", " rsp-rbp", "show decimal value of math expr",
	"?V", "", "show library version of rz_core",
	"?w", " addr", "show what's in this address (like pxr/pxq does)",
	"?X", " num|expr", "returns the hexadecimal value numeric expr",
	"?x", " str", "returns the hexpair of number or string",
	"?x", "+num", "like ?v, but in hexpairs honoring cfg.bigendian",
	"?x", "-hexst", "convert hexpair into raw string with newline",
	"?_", " hudfile", "load hud menu with given file",
	"[cmd]?*", "", "recursive help for the given cmd",
	NULL
};

static const char *help_msg_question_v[] = {
	"Usage: ?v [$.]", "", "",
	"flag", "", "offset of flag",
	"$", "{ev}", "get value of eval config variable",
	"$$", "", "here (current virtual seek)",
	"$$$", "", "current non-temporary virtual seek",
	"$?", "", "last comparison value",
	"$alias", "=value", "alias commands (simple macros)",
	"$B", "", "base address (aligned lowest map address)",
	"$b", "", "block size",
	"$c", "", "get terminal width in character columns",
	"$Cn", "", "get nth call of function",
	"$D", "", "current debug map base address ?v $D @ rsp",
	"$DB", "", "same as dbg.baddr, progam base address",
	"$DD", "", "current debug map size",
	"$Dn", "", "get nth data reference in function",
	"$e", "", "1 if end of block, else 0",
	"$e", "{flag}", "end of flag (flag->offset + flag->size)",
	"$f", "", "jump fail address (e.g. jz 0x10 => next instruction)",
	"$F", "", "Same as $FB",
	"$Fb", "", "begin of basic block",
	"$FB", "", "begin of function",
	"$Fe", "", "end of basic block",
	"$FE", "", "end of function",
	"$Ff", "", "function false destination",
	"$Fi", "", "basic block instructions",
	"$FI", "", "function instructions",
	"$Fj", "", "function jump destination",
	"$fl", "", "flag length (size) at current address (fla; pD $l @ entry0)",
	"$FS", "", "function size (linear length)",
	"$Fs", "", "size of the current basic block",
	"$FSS", "", "function size (sum bb sizes)",
	"$j", "", "jump address (e.g. jmp 0x10, jz 0x10 => 0x10)",
	"$Ja", "", "get nth jump of function",
	"$k{kv}", "", "get value of an sdb query value",
	"$l", "", "opcode length",
	"$M", "", "map address (lowest map address)",
	"$m", "", "opcode memory reference (e.g. mov eax,[0x10] => 0x10)",
	"$MM", "", "map size (lowest map address)",
	"$O", "", "cursor here (current offset pointed by the cursor)",
	"$o", "", "here (current disk io offset)",
	"$p", "", "getpid()",
	"$P", "", "pid of children (only in debug)",
	"$r", "", "get console height (in rows, see $c for columns)",
	"$r", "{reg}", "get value of named register",
	"$s", "", "file size",
	"$S", "", "section offset",
	"$SS", "", "section size",
	"$s", "{flag}", "get size of flag",
	"$v", "", "opcode immediate value (e.g. lui a0,0x8010 => 0x8010)",
	"$w", "", "get word size, 4 if asm.bits=32, 8 if 64, ...",
	"$Xn", "", "get nth xref of function",
	"RzNum", "", "$variables usable in math expressions",
	NULL
};

static const char *help_msg_question_V[] = {
	"Usage: ?V[jq]", "", "",
	"?V", "", "show version information",
	"?V0", "", "show major version",
	"?V1", "", "show minor version",
	"?V2", "", "show patch version",
	"?Vn", "", "show numeric version (2)",
	"?Vc", "", "show numeric version",
	"?Vj", "", "same as above but in JSON",
	"?Vq", "", "quiet mode, just show the version number",
	NULL
};

static const char *help_msg_greater_sign[] = {
	"Usage:", "[cmd]>[file]", "redirects console from 'cmd' output to 'file'",
	"[cmd] > [file]", "", "redirect STDOUT of 'cmd' to 'file'",
	"[cmd] > $alias", "", "save the output of the command as an alias (see $?)",
	"[cmd] H> [file]", "", "redirect html output of 'cmd' to 'file'",
	"[cmd] 2> [file]", "", "redirect STDERR of 'cmd' to 'file'",
	"[cmd] 2> /dev/null", "", "omit the STDERR output of 'cmd'",
	NULL
};

static void cmd_help_exclamation(RzCore *core) {
	rz_core_cmd_help(core, help_msg_exclamation);
	rz_core_cmd_help(core, help_msg_env);
}

static void cmd_help_percent(RzCore *core) {
	rz_core_cmd_help(core, help_msg_percent);
	rz_core_cmd_help(core, help_msg_env);
}

static const char *findBreakChar(const char *s) {
	while (*s) {
		if (!rz_name_validate_char(*s, true)) {
			break;
		}
		s++;
	}
	return s;
}

static char *filterFlags(RzCore *core, const char *msg) {
	const char *dollar, *end;
	char *word, *buf = NULL;
	for (;;) {
		dollar = strchr(msg, '$');
		if (!dollar) {
			break;
		}
		buf = rz_str_appendlen(buf, msg, dollar - msg);
		if (dollar[1] == '{') {
			// find }
			end = strchr(dollar + 2, '}');
			if (end) {
				word = rz_str_newlen(dollar + 2, end - dollar - 2);
				end++;
			} else {
				msg = dollar + 1;
				buf = rz_str_append(buf, "$");
				continue;
			}
		} else {
			end = findBreakChar(dollar + 1);
			if (!end) {
				end = dollar + strlen(dollar);
			}
			word = rz_str_newlen(dollar + 1, end - dollar - 1);
		}
		if (end && word) {
			ut64 val = rz_num_math(core->num, word);
			char num[32];
			snprintf(num, sizeof(num), "0x%" PFMT64x, val);
			buf = rz_str_append(buf, num);
			msg = end;
		} else {
			break;
		}
		free(word);
	}
	buf = rz_str_append(buf, msg);
	return buf;
}

static const char *avatar_orangg[] = {
	"      _______\n"
	"     /       \\      .-%s-.\n"
	"   _| ( o) (o)\\_    | %s |\n"
	"  / _     .\\. | \\  <| %s |\n"
	"  \\| \\   ____ / 7`  | %s |\n"
	"  '|\\|  `---'/      `-%s-'\n"
	"     | /----. \\\n"
	"     | \\___/  |___\n"
	"     `-----'`-----'\n"
};

static const char *avatar_clippy[] = {
	" .--.     .-%s-.\n"
	" | _|     | %s |\n"
	" | O O   <  %s |\n"
	" |  |  |  | %s |\n"
	" || | /   `-%s-'\n"
	" |`-'|\n"
	" `---'\n",
	" .--.     .-%s-.\n"
	" |   \\    | %s |\n"
	" | O o   <  %s |\n"
	" |   | /  | %s |\n"
	" |  ( /   `-%s-'\n"
	" |   / \n"
	" `--'\n",
	" .--.     .-%s-.\n"
	" | _|_    | %s |\n"
	" | O O   <  %s |\n"
	" |  ||    | %s |\n"
	" | _:|    `-%s-'\n"
	" |   |\n"
	" `---'\n",
};

static const char *avatar_clippy_utf8[] = {
	" ╭──╮    ╭─%s─╮\n"
	" │ _│    │ %s │\n"
	" │ O O  <  %s │\n"
	" │  │╭   │ %s │\n"
	" ││ ││   ╰─%s─╯\n"
	" │└─┘│\n"
	" ╰───╯\n",
	" ╭──╮    ╭─%s─╮\n"
	" │ ╶│╶   │ %s │\n"
	" │ O o  <  %s │\n"
	" │  │  ╱ │ %s │\n"
	" │ ╭┘ ╱  ╰─%s─╯\n"
	" │ ╰ ╱\n"
	" ╰──'\n",
	" ╭──╮    ╭─%s─╮\n"
	" │ _│_   │ %s │\n"
	" │ O O  <  %s │\n"
	" │  │╷   │ %s │\n"
	" │  ││   ╰─%s─╯\n"
	" │ ─╯│\n"
	" ╰───╯\n",
};

static const char *avatar_cybcat[] = {
	"     /\\.---./\\       .-%s-.\n"
	" '--           --'   | %s |\n"
	"----   ^   ^   ---- <  %s |\n"
	"  _.-    Y    -._    | %s |\n"
	"                     `-%s-'\n",
	"     /\\.---./\\       .-%s-.\n"
	" '--   @   @   --'   | %s |\n"
	"----     Y     ---- <  %s |\n"
	"  _.-    O    -._    | %s |\n"
	"                     `-%s-'\n",
	"     /\\.---./\\       .-%s-.\n"
	" '--   =   =   --'   | %s |\n"
	"----     Y     ---- <  %s |\n"
	"  _.-    U    -._    | %s |\n"
	"                     `-%s-'\n",
};

enum {
	RZ_AVATAR_ORANGG,
	RZ_AVATAR_CYBCAT,
	RZ_AVATAR_CLIPPY,
};

/**
 * \brief Returns all the $ variable names in a NULL-terminated array.
 */
RZ_API const char **rz_core_help_vars_get(RzCore *core) {
	static const char *vars[] = {
		"$$", "$$$", "$?", "$B", "$b", "$c", "$Cn", "$D", "$DB", "$DD", "$Dn",
		"$e", "$f", "$F", "$Fb", "$FB", "$Fe", "$FE", "$Ff", "$Fi", "$FI", "$Fj",
		"$fl", "$FS", "$Fs", "$FSS", "$j", "$Ja", "$l", "$M", "$m", "$MM", "$O",
		"$o", "$p", "$P", "$r", "$s", "$S", "$SS", "$v", "$w", "$Xn", NULL
	};
	return vars;
}

RZ_API void rz_core_help_vars_print(RzCore *core) {
	int i = 0;
	const char **vars = rz_core_help_vars_get(core);
	const bool wideOffsets = rz_config_get_i(core->config, "scr.wideoff");
	while (vars[i]) {
		const char *pad = rz_str_pad(' ', 6 - strlen(vars[i]));
		if (wideOffsets) {
			rz_cons_printf("%s %s 0x%016" PFMT64x "\n", vars[i], pad, rz_num_math(core->num, vars[i]));
		} else {
			rz_cons_printf("%s %s 0x%08" PFMT64x "\n", vars[i], pad, rz_num_math(core->num, vars[i]));
		}
		i++;
	}
}

/**
 * \brief Get clippy echo string.
 * \param msg The message to echo.
 */
RZ_API RZ_OWN char *rz_core_clippy(RZ_NONNULL RzCore *core, RZ_NONNULL const char *msg) {
	rz_return_val_if_fail(core && msg, NULL);
	int type = RZ_AVATAR_CLIPPY;
	if (*msg == '+' || *msg == '3') {
		char *space = strchr(msg, ' ');
		if (!space) {
			return NULL;
		}
		type = (*msg == '+') ? RZ_AVATAR_ORANGG : RZ_AVATAR_CYBCAT;
		msg = space + 1;
	}
	const char *f;
	int msglen = rz_str_len_utf8(msg);
	char *s = strdup(rz_str_pad(' ', msglen));
	char *l;

	if (type == RZ_AVATAR_ORANGG) {
		l = strdup(rz_str_pad('-', msglen));
		f = avatar_orangg[0];
	} else if (type == RZ_AVATAR_CYBCAT) {
		l = strdup(rz_str_pad('-', msglen));
		f = avatar_cybcat[rz_num_rand(RZ_ARRAY_SIZE(avatar_cybcat))];
	} else if (rz_config_get_i(core->config, "scr.utf8")) {
		l = (char *)rz_str_repeat("─", msglen);
		f = avatar_clippy_utf8[rz_num_rand(RZ_ARRAY_SIZE(avatar_clippy_utf8))];
	} else {
		l = strdup(rz_str_pad('-', msglen));
		f = avatar_clippy[rz_num_rand(RZ_ARRAY_SIZE(avatar_clippy))];
	}

	char *string = rz_str_newf(f, l, s, msg, s, l);
	free(l);
	free(s);
	return string;
}

RZ_IPI void rz_core_clippy_print(RzCore *core, const char *msg) {
	char *string = rz_core_clippy(core, msg);
	if (string) {
		rz_cons_print(string);
		free(string);
	}
}

RZ_IPI int rz_cmd_help(void *data, const char *input) {
	RzCore *core = (RzCore *)data;
	RzIOMap *map;
	const char *k;
	RzListIter *iter;
	char *p, out[128] = RZ_EMPTY;
	ut64 n;
	int i;
	RzList *tmp;

	switch (input[0]) {
	case '0': // "?0"
		core->curtab = 0;
		break;
	case '1': // "?1"
		if (core->curtab < 0) {
			core->curtab = 0;
		}
		core->curtab++;
		break;
	case 'r': // "?r"
	{ // TODO : Add support for 64bit random numbers
		ut64 b = 0;
		ut32 r = UT32_MAX;
		if (input[1]) {
			strncpy(out, input + (input[1] == ' ' ? 2 : 1), sizeof(out) - 1);
			p = strchr(out + 1, ' ');
			if (p) {
				*p = 0;
				b = (ut32)rz_num_math(core->num, out);
				r = (ut32)rz_num_math(core->num, p + 1) - b;
			} else {
				r = (ut32)rz_num_math(core->num, out);
			}
		} else {
			r = 0LL;
		}
		if (!r) {
			r = UT32_MAX >> 1;
		}
		core->num->value = (ut64)(b + rz_num_rand(r));
		rz_cons_printf("0x%" PFMT64x "\n", core->num->value);
	} break;
	case 'a': // "?a"
		rz_cons_printf("%s", ret_ascii_table());
		break;
	case 'b': // "?b"
		if (input[1] == '6' && input[2] == '4') {
			// b64 decoding takes at most strlen(str) * 4
			const int buflen = (strlen(input + 3) * 4) + 1;
			char *buf = calloc(buflen, sizeof(char));
			if (!buf) {
				return false;
			}
			if (input[3] == '-') {
				rz_base64_decode((ut8 *)buf, input + 4, -1);
			} else if (input[3] == ' ') {
				const char *s = input + 4;
				rz_base64_encode(buf, (const ut8 *)s, strlen(s));
			}
			rz_cons_println(buf);
			free(buf);
		} else if (input[1] == 't' && input[2] == 'w') { // "?btw"
			if (rz_num_between(core->num, input + 3) == -1) {
				RZ_LOG_ERROR("core: Usage: ?btw num|(expr) num|(expr) num|(expr)\n");
			}
		} else {
			n = rz_num_math(core->num, input + 1);
			rz_num_to_bits(out, n);
			rz_cons_printf("%sb\n", out);
		}
		break;
	case 'B': // "?B"
		k = rz_str_trim_head_ro(input + 1);
		tmp = rz_core_get_boundaries_prot(core, -1, k, "search");
		if (!tmp) {
			return false;
		}
		rz_list_foreach (tmp, iter, map) {
			rz_cons_printf("0x%" PFMT64x " 0x%" PFMT64x "\n", map->itv.addr, rz_itv_end(map->itv));
		}
		rz_list_free(tmp);
		break;
	case 'h': // "?h"
		if (input[1] == ' ') {
			ut32 hash = (ut32)rz_str_djb2_hash(input + 2);
			rz_cons_printf("0x%08x\n", hash);
		} else {
			RZ_LOG_ERROR("core: Usage: ?h [string-to-hash]\n");
		}
		break;
	case 'F': // "?F"
		rz_cons_flush();
		break;
	case 'f': // "?f"
		if (input[1] == ' ') {
			char *q, *p = strdup(input + 2);
			if (!p) {
				RZ_LOG_ERROR("core: Cannot strdup\n");
				return 0;
			}
			q = strchr(p, ' ');
			if (q) {
				*q = 0;
				n = rz_num_get(core->num, p);
				rz_str_bits(out, (const ut8 *)&n, sizeof(n) * 8, q + 1);
				rz_cons_println(out);
			} else {
				RZ_LOG_ERROR("core: Usage: \"?b value bitstring\"\n");
			}
			free(p);
		} else {
			RZ_LOG_ERROR("core: Whitespace expected after '?f'\n");
		}
		break;
	case 'o': // "?o"
		n = rz_num_math(core->num, input + 1);
		rz_cons_printf("0%" PFMT64o "\n", n);
		break;
	case 'T': // "?T"
		rz_cons_printf("plug.init = %" PFMT64d "\n"
			       "plug.load = %" PFMT64d "\n"
			       "file.load = %" PFMT64d "\n",
			core->times->loadlibs_init_time,
			core->times->loadlibs_time,
			core->times->file_open_time);
		break;
	case 'u': // "?u"
	{
		char unit[8];
		n = rz_num_math(core->num, input + 1);
		rz_num_units(unit, sizeof(unit), n);
		rz_cons_println(unit);
	} break;
	case 'j': // "?j"
	case ' ': // "? "
	{
		char *asnum, unit[8];
		ut32 s, a;
		double d;
		float f;
		char number[128];
		char *const inputs = strdup(input + 1);
		RzList *list = rz_num_str_split_list(inputs);
		const int list_len = rz_list_length(list);
		PJ *pj = NULL;
		if (*input == 'j') {
			pj = pj_new();
			pj_o(pj);
		}
		for (i = 0; i < list_len; i++) {
			const char *str = rz_list_pop_head(list);
			if (!*str) {
				continue;
			}
			n = rz_num_math(core->num, str);
			if (core->num->dbz) {
				RZ_LOG_ERROR("core: RzNum ERROR: Division by Zero\n");
			}
			asnum = rz_num_as_string(NULL, n, false);
			/* decimal, hexa, octal */
			s = n >> 16 << 12;
			a = n & 0x0fff;
			rz_num_units(unit, sizeof(unit), n);
			if (*input == 'j') {
				pj_ks(pj, "int32", rz_strf(number, "%d", (st32)(n & UT32_MAX)));
				pj_ks(pj, "uint32", rz_strf(number, "%u", (ut32)n));
				pj_ks(pj, "int64", rz_strf(number, "%" PFMT64d, (st64)n));
				pj_ks(pj, "uint64", rz_strf(number, "%" PFMT64u, (ut64)n));
				pj_ks(pj, "hex", rz_strf(number, "0x%08" PFMT64x, n));
				pj_ks(pj, "octal", rz_strf(number, "0%" PFMT64o, n));
				pj_ks(pj, "unit", unit);
				pj_ks(pj, "segment", rz_strf(number, "%04x:%04x", s, a));

			} else {
				if (n >> 32) {
					rz_cons_printf("int64   %" PFMT64d "\n", (st64)n);
					rz_cons_printf("uint64  %" PFMT64u "\n", (ut64)n);
				} else {
					rz_cons_printf("int32   %d\n", (st32)n);
					rz_cons_printf("uint32  %u\n", (ut32)n);
				}
				rz_cons_printf("hex     0x%" PFMT64x "\n", n);
				rz_cons_printf("octal   0%" PFMT64o "\n", n);
				rz_cons_printf("unit    %s\n", unit);
				rz_cons_printf("segment %04x:%04x\n", s, a);

				if (asnum) {
					rz_cons_printf("string  \"%s\"\n", asnum);
					free(asnum);
				}
			}
			/* binary and floating point */
			rz_str_bits64(out, n);
			f = d = core->num->fvalue;
			/* adjust sign for nan floats, different libcs are confused */
			if (isnan(f) && signbit(f)) {
				f = -f;
			}
			if (isnan(d) && signbit(d)) {
				d = -d;
			}
			if (*input == 'j') {
				pj_ks(pj, "fvalue", rz_strf(number, "%.1lf", core->num->fvalue));
				pj_ks(pj, "float", rz_strf(number, "%ff", f));
				pj_ks(pj, "double", rz_strf(number, "%lf", d));
				pj_ks(pj, "binary", rz_strf(number, "0b%s", out));
				rz_num_to_trits(out, n);
				pj_ks(pj, "trits", rz_strf(number, "0t%s", out));
			} else {
				rz_cons_printf("fvalue  %.1lf\n", core->num->fvalue);
				rz_cons_printf("float   %ff\n", f);
				rz_cons_printf("double  %lf\n", d);
				rz_cons_printf("binary  0b%s\n", out);

				/* ternary */
				rz_num_to_trits(out, n);
				rz_cons_printf("trits   0t%s\n", out);
			}
		}
		if (*input == 'j') {
			pj_end(pj);
		}
		free(inputs);
		rz_list_free(list);
		if (pj) {
			rz_cons_printf("%s\n", pj_string(pj));
			pj_free(pj);
		}
	} break;
	case 'q': // "?q"
		if (core->num->dbz) {
			RZ_LOG_ERROR("core: RzNum ERROR: Division by Zero\n");
		}
		if (input[1] == '?') {
			rz_cons_printf("|Usage: ?q [num]  # Update $? without printing anything\n"
				       "|?q 123; ?? x    # hexdump if 123 != 0");
		} else {
			const char *space = strchr(input, ' ');
			if (space) {
				n = rz_num_math(core->num, space + 1);
			} else {
				n = rz_num_math(core->num, "$?");
			}
			core->num->value = n; // redundant
		}
		break;
	case 'v': // "?v"
	{
		const char *space = strchr(input, ' ');
		if (space) {
			n = rz_num_math(core->num, space + 1);
		} else {
			n = rz_num_math(core->num, "$?");
		}
	}
		if (core->num->dbz) {
			RZ_LOG_ERROR("core: RzNum ERROR: Division by Zero\n");
		}
		switch (input[1]) {
		case '?':
			rz_cons_printf("|Usage: ?v[id][ num]  # Show value\n"
				       "|?vx number  -> show 8 digit padding in hex\n"
				       "|?vi1 200    -> 1 byte size value (char)\n"
				       "|?vi2 0xffff -> 2 byte size value (short)\n"
				       "|?vi4 0xffff -> 4 byte size value (int)\n"
				       "|?vi8 0xffff -> 8 byte size value (st64)\n"
				       "| No argument shows $? value\n"
				       "|?vi will show in decimal instead of hex\n");
			break;
		case '\0':
			rz_cons_printf("%d\n", (st32)n);
			break;
		case 'x': // "?vx"
			rz_cons_printf("0x%08" PFMT64x "\n", n);
			break;
		case 'i': // "?vi"
			switch (input[2]) {
			case '1': // byte
				rz_cons_printf("%d\n", (st8)(n & UT8_MAX));
				break;
			case '2': // word
				rz_cons_printf("%d\n", (st16)(n & UT16_MAX));
				break;
			case '4': // dword
				rz_cons_printf("%d\n", (st32)(n & UT32_MAX));
				break;
			case '8': // qword
				rz_cons_printf("%" PFMT64d "\n", (st64)(n & UT64_MAX));
				break;
			default:
				rz_cons_printf("%" PFMT64d "\n", n);
				break;
			}
			break;
		case 'd':
			rz_cons_printf("%" PFMT64d "\n", n);
			break;
		default:
			rz_cons_printf("0x%" PFMT64x "\n", n);
		}
		core->num->value = n; // redundant
		break;
	case '=': // "?=" set num->value
		if (input[1] == '=') { // ?==
			if (input[2] == ' ') {
				char *s = strdup(input + 3);
				char *e = strchr(s, ' ');
				if (e) {
					*e++ = 0;
					core->num->value = strcmp(s, e);
				} else {
					RZ_LOG_ERROR("core: Missing secondary word in expression to compare\n");
				}
				free(s);
			} else {
				RZ_LOG_ERROR("core: Usage: ?== str1 str2\n");
			}
		} else {
			if (input[1]) { // ?=
				rz_num_math(core->num, input + 1);
			} else {
				rz_cons_printf("0x%" PFMT64x "\n", core->num->value);
			}
		}
		break;
	case '+': // "?+"
		if (input[1]) {
			st64 n = (st64)core->num->value;
			if (n > 0) {
				rz_core_cmd(core, input + 1, 0);
			}
		} else {
			rz_cons_printf("0x%" PFMT64x "\n", core->num->value);
		}
		break;
	case '-': // "?-"
		if (input[1]) {
			st64 n = (st64)core->num->value;
			if (n < 0) {
				rz_core_cmd(core, input + 1, 0);
			}
		} else {
			rz_cons_printf("0x%" PFMT64x "\n", core->num->value);
		}
		break;
	case '!': // "?!"
		if (input[1]) {
			if (!core->num->value) {
				if (input[1] == '?') {
					cmd_help_exclamation(core);
					return 0;
				}
				return core->num->value = rz_core_cmd(core, input + 1, 0);
			}
		} else {
			rz_cons_printf("0x%" PFMT64x "\n", core->num->value);
		}
		break;
	case '@': // "?@"
		if (input[1] == '@') {
			if (input[2] == '@') {
				rz_core_cmd_help(core, help_msg_at_at_at);
			} else {
				rz_core_cmd_help(core, help_msg_at_at);
			}
		} else {
			rz_core_cmd_help(core, help_msg_at);
		}
		break;
	case '&': // "?&"
		helpCmdTasks(core);
		break;
	case '%': // "?%"
		if (input[1] == '?') {
			cmd_help_percent(core);
		}
		break;
	case '$': // "?$"
		if (input[1] == '?') {
			rz_core_cmd_help(core, help_msg_question_v);
		} else {
			rz_core_help_vars_print(core);
		}
		return true;
	case 'V': // "?V"
		switch (input[1]) {
		case '?': // "?V?"
			rz_core_cmd_help(core, help_msg_question_V);
			break;
		case 0: { // "?V"
			char *v = rz_version_str(NULL);
			rz_cons_printf("%s\n", v);
			free(v);
			break;
		}
		case 'c': // "?Vc"
			rz_cons_printf("%d\n", vernum(RZ_VERSION));
			break;
		case 'j': // "?Vj"
		{
			PJ *pj = pj_new();
			pj_o(pj);
			pj_ks(pj, "arch", RZ_SYS_ARCH);
			pj_ks(pj, "os", RZ_SYS_OS);
			pj_ki(pj, "bits", RZ_SYS_BITS);
			pj_ki(pj, "major", RZ_VERSION_MAJOR);
			pj_ki(pj, "minor", RZ_VERSION_MINOR);
			pj_ki(pj, "patch", RZ_VERSION_PATCH);
			pj_ki(pj, "number", RZ_VERSION_NUMBER);
			pj_ki(pj, "nversion", vernum(RZ_VERSION));
			pj_ks(pj, "version", RZ_VERSION);
			pj_end(pj);
			rz_cons_printf("%s\n", pj_string(pj));
			pj_free(pj);
		} break;
		case 'n': // "?Vn"
			rz_cons_printf("%d\n", RZ_VERSION_NUMBER);
			break;
		case 'q': // "?Vq"
			rz_cons_println(RZ_VERSION);
			break;
		case '0':
			rz_cons_printf("%d\n", RZ_VERSION_MAJOR);
			break;
		case '1':
			rz_cons_printf("%d\n", RZ_VERSION_MINOR);
			break;
		case '2':
			rz_cons_printf("%d\n", RZ_VERSION_PATCH);
			break;
		}
		break;
	case 'l': // "?l"
		if (input[1] == 'q') {
			for (input += 2; input[0] == ' '; input++)
				;
			core->num->value = strlen(input);
		} else {
			for (input++; input[0] == ' '; input++)
				;
			core->num->value = strlen(input);
			rz_cons_printf("%" PFMT64d "\n", core->num->value);
		}
		break;
	case 'X': // "?X"
		for (input++; input[0] == ' '; input++)
			;
		n = rz_num_math(core->num, input);
		rz_cons_printf("%" PFMT64x "\n", n);
		break;
	case 'x': // "?x"
		for (input++; input[0] == ' '; input++)
			;
		if (*input == '-') {
			ut8 *out = malloc(strlen(input) + 1);
			if (out) {
				int len = rz_hex_str2bin(input + 1, out);
				if (len >= 0) {
					out[len] = 0;
					rz_cons_println((const char *)out);
				} else {
					RZ_LOG_ERROR("core: Error parsing the hexpair string\n");
				}
				free(out);
			}
		} else if (*input == '+') {
			ut64 n = rz_num_math(core->num, input);
			int bits = rz_num_to_bits(NULL, n) / 8;
			for (i = 0; i < bits; i++) {
				rz_cons_printf("%02x", (ut8)((n >> (i * 8)) & 0xff));
			}
			rz_cons_newline();
		} else {
			if (*input == ' ') {
				input++;
			}
			for (i = 0; input[i]; i++) {
				rz_cons_printf("%02x", input[i]);
			}
			rz_cons_newline();
		}
		break;
	case 'E': // "?E" clippy echo
		rz_core_clippy_print(core, rz_str_trim_head_ro(input + 1));
		break;
	case 'e': // "?e" echo
		switch (input[1]) {
		case 't': // "?et newtitle"
			rz_cons_set_title(rz_str_trim_head_ro(input + 2));
			break;
		case '=': { // "?e="
			ut64 pc = rz_num_math(core->num, input + 2);
			rz_print_progressbar(core->print, pc, 80);
			rz_cons_newline();
			break;
		}
		case 'c': // "?ec" column
			rz_cons_column(rz_num_math(core->num, input + 2));
			break;
		case 'g': { // "?eg" gotoxy
			int x = atoi(input + 2);
			char *arg = strchr(input + 2, ' ');
			int y = arg ? atoi(arg + 1) : 0;
			rz_cons_gotoxy(x, y);
		} break;
		case 'n': { // "?en" echo -n
			const char *msg = rz_str_trim_head_ro(input + 2);
			// TODO: replace all ${flagname} by its value in hexa
			char *newmsg = filterFlags(core, msg);
			rz_str_unescape(newmsg);
			rz_cons_print(newmsg);
			free(newmsg);
			break;
		}
		case ' ': {
			const char *msg = rz_str_trim_head_ro(input + 1);
			// TODO: replace all ${flagname} by its value in hexa
			char *newmsg = filterFlags(core, msg);
			rz_str_unescape(newmsg);
			rz_cons_println(newmsg);
			free(newmsg);
		} break;
		case 0:
			rz_cons_newline();
			break;
		default:
			rz_core_cmd_help(core, help_msg_question_e);
			break;
		}
		break;
	case 's': { // "?s" sequence from to step
		ut64 from, to, step;
		char *p, *p2;
		for (input++; *input == ' '; input++)
			;
		p = strchr(input, ' ');
		if (p) {
			*p = '\0';
			from = rz_num_math(core->num, input);
			p2 = strchr(p + 1, ' ');
			if (p2) {
				*p2 = '\0';
				step = rz_num_math(core->num, p2 + 1);
			} else {
				step = 1;
			}
			to = rz_num_math(core->num, p + 1);
			for (; from <= to; from += step)
				rz_cons_printf("%" PFMT64d " ", from);
			rz_cons_newline();
		}
		break;
	}
	case 'P': { // "?P" physical to virtual address conversion
		ut64 n = (input[0] && input[1]) ? rz_num_math(core->num, input + 2) : core->offset;
		ut64 vaddr = rz_io_p2v(core->io, n);
		if (vaddr == UT64_MAX) {
			rz_cons_printf("no map at 0x%08" PFMT64x "\n", n);
		} else {
			rz_cons_printf("0x%08" PFMT64x "\n", vaddr);
		}
		break;
	}
	case 'p': { // "?p" virtual to physical address conversion
		ut64 n = (input[0] && input[1]) ? rz_num_math(core->num, input + 2) : core->offset;
		ut64 paddr = rz_io_v2p(core->io, n);
		if (paddr == UT64_MAX) {
			rz_cons_printf("no map at 0x%08" PFMT64x "\n", n);
		} else {
			rz_cons_printf("0x%08" PFMT64x "\n", paddr);
		}
		break;
	}
	case '_': // "?_" hud input
		rz_core_yank_hud_file(core, input + 1);
		break;
	case 'i': // "?i" input num
		rz_cons_set_raw(0);
		if (!rz_cons_is_interactive()) {
			RZ_LOG_ERROR("core: Not running in interactive mode\n");
		} else {
			switch (input[1]) {
			case 'f': // "?if"
				core->num->value = !rz_num_conditional(core->num, input + 2);
				rz_cons_printf("%s\n", rz_str_bool(!core->num->value));
				break;
			case 'm': // "?im"
				rz_cons_message(input + 2);
				break;
			case 'p': // "?ip"
				core->num->value = rz_core_yank_hud_path(core, input + 2, 0) == true;
				break;
			case 'k': // "?ik"
				rz_cons_any_key(NULL);
				break;
			case 'y': // "?iy"
				for (input += 2; *input == ' '; input++)
					;
				core->num->value = rz_cons_yesno(1, "%s? (Y/n)", input);
				break;
			case 'n': // "?in"
				for (input += 2; *input == ' '; input++)
					;
				core->num->value = rz_cons_yesno(0, "%s? (y/N)", input);
				break;
			default: {
				char foo[1024];
				rz_cons_flush();
				for (input++; *input == ' '; input++)
					;
				// TODO: rz_cons_input()
				snprintf(foo, sizeof(foo) - 1, "%s: ", input);
				rz_line_set_prompt(foo);
				rz_cons_fgets(foo, sizeof(foo), 0, NULL);
				foo[sizeof(foo) - 1] = 0;
				rz_core_yank_set_str(core, RZ_CORE_FOREIGN_ADDR, foo);
				core->num->value = rz_num_math(core->num, foo);
			} break;
			}
		}
		rz_cons_set_raw(0);
		break;
	case 'w': { // "?w"
		ut64 addr = rz_num_math(core->num, input + 1);
		char *rstr = core->print->hasrefs(core->print->user, addr, true);
		if (!rstr) {
			RZ_LOG_ERROR("core: Cannot get refs\n");
			break;
		}
		rz_cons_println(rstr);
		free(rstr);
		break;
	}
	case 't': { // "?t"
		ut64 start = rz_time_now_mono();
		rz_core_cmd(core, input + 1, 0);
		ut64 end = rz_time_now_mono();
		double seconds = (double)(end - start) / RZ_USEC_PER_SEC;
		core->num->value = (ut64)seconds;
		rz_cons_printf("%lf\n", seconds);
		break;
	}
	case '?': // "??"
		if (input[1] == '?') {
			if (input[2] == '?') { // "???"
				rz_core_clippy_print(core, "What are you doing?");
				return 0;
			}
			if (input[2]) {
				if (core->num->value) {
					rz_core_cmd(core, input + 1, 0);
				}
				break;
			}
			rz_core_cmd_help(core, help_msg_question);
			return 0;
		} else if (input[1]) {
			if (core->num->value) {
				core->num->value = rz_core_cmd(core, input + 1, 0);
			}
		} else {
			if (core->num->dbz) {
				RZ_LOG_ERROR("core: RzNum ERROR: Division by Zero\n");
			}
			rz_cons_printf("%" PFMT64d "\n", core->num->value);
		}
		break;
	case '\0': // "?"
	default:
		break;
	}
	return 0;
}
