
/*
 * Copyright (C) 2013 Palmer Dabbelt
 *   <palmer@dabbelt.com>
 *
 * This file is part of vcd2step.
 *
 * vcd2step is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * vcd2step is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with vcd2step.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <libvcd/vcd.h++>
#if FLO
#include <libflo/flo.h++>
#endif
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unordered_map>
#include <gmpxx.h>
#include "version.h"

#if FLO
typedef libflo::node node;
typedef libflo::operation<node> operation;
typedef libflo::flo<node, operation> flo;
#endif

/* Name mangles a VCD name (with "::" or ":" as a seperator) into a
 * Chisel name (with "." as a seperator). */
static const std::string vcd2chisel(const std::string& vcd_name);

/* Converts a binary-encoded string to a decimal-encoded string. */
static const std::string bits2int(const std::string& value_bits);
//static const std::string bits2int(const std::string& value_bits,
//                                  size_t start, size_t end);

static const std::string baseName(const std::string path);

int main(int argc, const char **argv)
{
    if (argc == 2 && (strcmp(argv[1], "--version") == 0)) {
        printf("vcd2Tester " PCONFIGURE_VERSION "\n");
        exit(0);
    }
#if FLO
    const int minArgc = 4;
#else
    const int minArgc = 3;
#endif
    if ((argc == 2 && (strcmp(argv[1], "--help") == 0)) || argc != minArgc) {
#if FLO
        printf("vcd2Tester <TOP.vcd> <TOP.flo> <TOP.step>: Converts from VCD to FirtlTerp Tester\n"
#else
                printf("vcd2Tester <TOP.vcd> <TOP.step>: Converts from VCD to FirtlTerp Tester\n"
#endif
               "  vcd2FINTTester converts a VCD file to a FIRRTL interpreter tester file\n"
               "\n"
               "  --version: Print the version number and exit\n"
               "  --help:    Print this help text and exit\n"
            );
        exit(0);
    }

    const auto prolog = R"(
import org.scalatest.{Matchers, FlatSpec}
import %s._

class %s extends FlatSpec with Matchers {
  it should "run with InterpretedTester too" in {
    val x = new InterpretiveTester(%s) {

)";
    const auto epilog = R"(
  }
}
)";
    /* Open the two files that we were given. */
    const int VCDFILE = 1;
    libvcd::vcd vcd(argv[VCDFILE]);

#if FLO
    const int FLOFILE = 2;
    auto flo = flo::parse(argv[FLOFILE]);
    const auto moduleName = baseName(std::string(argv[FLOFILE]));
    const int OUTFILE = 3;
#else
    const int OUTFILE = 2;
    const std::string moduleName = "Torture";
#endif
    const auto fileName = std::string(argv[OUTFILE]);
    const auto className = baseName(fileName);
    auto step = fopen(fileName.c_str(), "w");
    fprintf(step, prolog, moduleName.c_str(), className.c_str(), moduleName.c_str());
    const char * indent = "    ";
    const std::string::size_type moduleNameLength = moduleName.length();

    /* Build a map that contains the list of names that will be output
     * to the poke file. */
    std::unordered_map<std::string, bool> should_poke;
#if FLO
    for (const auto& op: flo->operations())
        if (op->op() == libflo::opcode::IN)
            should_poke[vcd2chisel(op->d()->name())] = true;
#else
    for (const auto& vcd_name: vcd.all_long_names()) {
        auto chisel_name = vcd2chisel(vcd_name);
        std::string matchName = moduleName + ".io_in";
        const char * op;
        if (chisel_name.find(matchName) == 0) {
        	should_poke[chisel_name] = true;
        	op = "poke";
        } else {
            op = "peek";
        }
//        printf("%s: %s -> %s (%s)\n", op, vcd_name.c_str(), chisel_name.c_str(), matchName.c_str());
    }
#endif

    /* The remainder of the circuit can be computed from just its
     * inputs on every cycle.  Those can all be obtained from the VCD
     * alone. */

    /* Read all the way through the VCD file, */
    while (vcd.has_more_cycles()) {
        vcd.step();

        for (const auto& vcd_name: vcd.all_long_names()) {
            auto chisel_name = vcd2chisel(vcd_name);

            auto value_bits = vcd.long_name_to_bits(vcd_name);

#if 0
            fprintf(stderr, "%s: %s\n",
                    chisel_name.c_str(),
                    value_bits.c_str()
                );
#endif

            auto value_int = bits2int(value_bits);
            // Strip the module name and "." from the signal name.
            auto cName = chisel_name.substr(moduleNameLength + 1);

            /* Poke inputs. */
            if (should_poke.find(chisel_name) != should_poke.end()) {
                fprintf(step, "%spoke(\"%s\", %s)\n",
                		indent,
    					cName.c_str(),
                        value_int.c_str()
                    );
            } else {
                fprintf(step, "%sexpect(\"%s\", %s)\n",
                		indent,
    					cName.c_str(),
                        value_int.c_str()
                    );
            }

        }

        fprintf(step, "%sstep(1)\n", indent);
    }

    fprintf(step, epilog/*, className.c_str(), className.c_str()*/);
    fclose(step);

    return 0;
}

const std::string vcd2chisel(const std::string& vcd_name)
{
    char buffer[LINE_MAX];
    strncpy(buffer, vcd_name.c_str(), LINE_MAX);

    for (size_t i = 0; i < strlen(buffer); ++i) {
        while (buffer[i] == ':' && buffer[i+1] == ':')
            memmove(buffer + i, buffer + i + 1, strlen(buffer + i));
        if (buffer[i] == ':')
            buffer[i] = '.';
    }

    return buffer;
}

#if 0
const std::string bits2int(const std::string& value_bits,
                           size_t start, size_t end)
{
    if (value_bits.c_str()[0] != 'b') {
        fprintf(stderr, "Non-binary string '%s'\n", value_bits.c_str());
        abort();
    }

    mpz_class gmp(value_bits.c_str() + 1, 2);
    mpz_class mask = gmp & ((1 << end) - 1);
    mpz_class shift = mask >> start;

    return shift.get_str(10);
}
#endif

const std::string bits2int(const std::string& value_bits)
{
    if (value_bits.c_str()[0] != 'b') {
        fprintf(stderr, "Non-binary string '%s'\n", value_bits.c_str());
        abort();
    }

    mpz_class gmp(value_bits.c_str() + 1, 2);
    return gmp.get_str(10);
}

const std::string baseName(const std::string path) {
	const std::string::size_type extension(path.find_last_of('.'));
	const std::string::size_type directory(path.find_last_of('/'));
	const std::string::size_type startP = directory != path.npos ? directory + 1 : 0;
	const std::string::size_type endP = (extension != path.npos ? extension - 1: path.length()) - directory;
//	printf("baseName of %s (%lu, %lu)\n", path.c_str(), startP, endP);
	return path.substr(startP, endP);
}
