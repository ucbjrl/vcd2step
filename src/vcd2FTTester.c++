
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
#include <getopt.h>
#include <unordered_map>
#include <gmpxx.h>
#include "version.h"

#if FLO
typedef libflo::node node;
typedef libflo::operation<node> operation;
typedef libflo::flo<node, operation> flo;
#endif

/* Name mangles a VCD name (with "::" or ":" as a separator) into a
 * Chisel name (with "." as a separator). */
static const std::string vcd2chisel(const std::string& vcd_name);

/* Converts a binary-encoded string to a decimal-encoded string. */
static const std::string bits2int(const std::string& value_bits);
//static const std::string bits2int(const std::string& value_bits,
//                                  size_t start, size_t end);

static const std::string baseName(const std::string path);
static const std::string replaceExtension(const std::string path, const std::string newExtension);
static const std::string noDirectory(const std::string path);

void print_usage(FILE *f)
{
#if FLO
        fprintf(f, "vcd2step <TOP.vcd> <TOP.flo> <TOP.step>: Converts from VCD to FirtlTerp Tester\n"
                "  vcd2step converts a VCD file to a Chisel tester file\n"
                "\n"
                "  --version: 		Print the version number and exit\n"
                "  --help:    		Print this help text and exit\n"
             );
#else
        fprintf(f, "vcd2FTTester [--chisel <TOP.scala>] <TOP.vcd> <TOP.step>: Converts from VCD to FirtlTerp Tester\n"
                "  vcd2FTTester converts a VCD file to a Chisel or FIRRTL interpreter tester file\n"
                "\n"
                "  --version: 		Print the version number and exit\n"
                "  --help:    		Print this help text and exit\n"
        		"  --firrtl file.firrtl:	Include the FIRRTL version of the DUT\n"
                "  --chisel file.scala:	Include the Chisel version of the DUT instead of using an \"import\"\n"
        		"Note: only one of --firrtl or --chisel should be specified"
             );
#endif
}

int main(int argc, char *const * argv)
{
    struct option options[] = {
        {"version", 0, NULL, 'v'},
        {"help",    0, NULL, 'h'},
        {"chisel",  1, NULL, 'c'},
        {"verbose", 0, NULL, 'V'},
        {"firrtl",  1, NULL, 'f'},
        {NULL,      0, NULL, 0}
    };
    int verbose = false;
#if FLO
    const int minArgc = 3;
#else
    const int minArgc = 2;
#endif
    char * chiselFileName = NULL;
    char * firrtlFileName = NULL;
    int opt;
    while ((opt = getopt_long(argc, argv, "", options, NULL)) > 0) {
        switch (opt) {
        case 'v':
            printf("vcd2Tester " PCONFIGURE_VERSION "\n");
            exit(0);
        case 'h':
            print_usage(stdout);
            exit(0);
        case 'c':
        	chiselFileName = optarg;
        	break;
        case 'V':
        	verbose = true;
        	break;
        case 'f':
        	firrtlFileName = optarg;
        	break;
       default:
            print_usage(stderr);
            exit(EXIT_FAILURE);
        }

    }
    if (optind + minArgc > argc) {
    	fprintf(stderr, "Insufficient arguments\n");
        print_usage(stderr);
        exit(EXIT_FAILURE);
    }

    if (chiselFileName && firrtlFileName) {
    	fprintf(stderr, "Can't specify both Chisel and FIRRTL files\n");
        print_usage(stderr);
        exit(EXIT_FAILURE);
    }
    const auto testerProlog = R"(

%s
import firrtl._
import firrtl.interpreter._
import org.scalatest.{Matchers, FlatSpec}
import scala.io.Source
import %s._

object %s {
  def main(args: Array[String]): Unit = {
)";

    const auto chiselProlog = R"(
    val circuit = Chisel.Driver.elaborate(() => new Torture())
    val circuitString = circuit.emit
//    println(circuitString)
    val dummy = new %s(circuitString)
  }
}

class %s(circuit: String) extends FlatSpec with Matchers {
  behavior of "%s"

)";

    const auto firrtlProlog = R"(
    val dummy = new %s
  }
}

class %s extends FlatSpec with Matchers {
  behavior of "%s"

  val circuit = """
)";

    const auto firrtlEpilog = R"(
"""
)";

    const auto testerEpilog = R"(
    val x = new InterpretiveTester(circuit) {
	  interpreter.setVerbose(%s)

      poke("reset", BigInt(1))
      step(1)
      poke("reset", BigInt(0))

      for (line <- Source.fromFile("%s").getLines()) {
        val fields = line.split(" ")
        val (op, port, value) = (fields(0), fields(1), fields(2))
        op match {
          case "e" => expect(port, BigInt(value))
          case "p" => poke(port, BigInt(value))
          case "s" => step(value.toInt)
          case _ => System.err.println("unrecognized line " + line)
        }
      }
      report()
    }
}
)";
    /* Open the files that we were given. */
    libvcd::vcd vcd(argv[optind]);

#if FLO
    const int FLOFILE = optind + 1;
    auto flo = flo::parse(argv[FLOFILE]);
    const auto moduleName = baseName(std::string(argv[FLOFILE]));
    const int OUTFILE = optind + 2;
#else
    const int OUTFILE = optind + 1;
    const std::string moduleName = chiselFileName != NULL ? baseName(chiselFileName): "Torture";
#endif
    const auto fileName = std::string(argv[OUTFILE]);
    const auto className = baseName(fileName);
    const auto dataName = replaceExtension(fileName, ".data");
    const auto runtimeDataName = noDirectory(dataName);
    auto step = fopen(fileName.c_str(), "w");
    auto data = fopen(dataName.c_str(), "w");
    const std::string import = chiselFileName != NULL ? "" : "import torture._";
	if (chiselFileName != NULL) {
		auto chiselFile = fopen(chiselFileName, "r");
		if (chiselFile == NULL) {
			perror(chiselFileName);
			exit(EXIT_FAILURE);
		}
		char buffer[4096];
		int n;
		while((n = fread(buffer, 1, sizeof(buffer), chiselFile)) > 0 ) {
			fwrite(buffer, 1, n, step);
		}
		fclose(chiselFile);
    } else {
    	fprintf(step, "package torture\n\n");

    }

    fprintf(step, testerProlog, import.c_str(), className.c_str(), className.c_str());
	if (firrtlFileName != NULL) {
		auto firrtlFile = fopen(firrtlFileName, "r");
		if (firrtlFile == NULL) {
			perror(firrtlFileName);
			exit(EXIT_FAILURE);
		}
	    fprintf(step, firrtlProlog, className.c_str(), className.c_str(), moduleName.c_str());
		char buffer[4096];
		int n;
		while((n = fread(buffer, 1, sizeof(buffer), firrtlFile)) > 0 ) {
			fwrite(buffer, 1, n, step);
		}
	    fprintf(step, firrtlEpilog);
		fclose(firrtlFile);
	} else {
	    fprintf(step, chiselProlog, className.c_str(), className.c_str(), moduleName.c_str());
	}
    fprintf(step, testerEpilog, verbose ? "true" : "false", runtimeDataName.c_str());
    fclose(step);
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
        if (chisel_name.find(matchName) == 0) {
        	should_poke[chisel_name] = true;
        }
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
                fprintf(data, "p %s %s\n",
    					cName.c_str(),
                        value_int.c_str()
                    );
            } else {
                fprintf(data, "e %s %s\n",
    					cName.c_str(),
                        value_int.c_str()
                    );
            }

        }

        fprintf(data, "s 1 1\n");
    }
    fclose(data);

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

const std::string noDirectory(const std::string path) {
	const std::string::size_type directory(path.find_last_of('/'));
	const std::string::size_type startP = directory != path.npos ? directory + 1 : 0;
	const std::string::size_type endP = path.length();
//	printf("baseName of %s (%lu, %lu)\n", path.c_str(), startP, endP);
	return path.substr(startP, endP);
}

const std::string replaceExtension(const std::string path, const std::string newExtension) {
	const std::string::size_type extension(path.find_last_of('.'));
	const std::string::size_type endP = (extension != path.npos ? extension : path.length());
//	printf("baseName of %s (%lu, %lu)\n", path.c_str(), startP, endP);
	return path.substr(0, endP) + newExtension;
}
