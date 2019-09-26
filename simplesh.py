#! /usr/bin/env python3
# -*- coding: utf-8; -*-

from enum import Enum

import argparse
import json
import os
import pexpect
import re
import subprocess
import sys
import tempfile

version = "v.0.19.1"

"""
    Testing `simplesh` v0.19.1 built on pexpect library:
    https://pexpect.readthedocs.io/en/stable/index.html

    Ampliación de Sistemas Operativos (Curso 2019/2020)
    Departamento de Ingeniería y Tecnología de Computadores
    Facultad de Informática de la Universidad de Murcia
"""


################################################################################


def info(*args):
    print("{}:".format(os.path.basename(sys.argv[0])), *args)


def panic(*args):
    info(*args)
    sys.exit(1)


################################################################################


class Range2List(argparse.Action):

    """ Helper class to parse lists of ranges. """

    def __call__(self, parser, namespace, test_ranges, option_strings=None):
        test_ranges = test_ranges.split(',')
        tests = []
        regex = re.compile('^(\d+)(?:-(\d+))?$')
        for test_range in test_ranges:
            m = regex.match(test_range)
            if not m:
                raise argparse.ArgumentTypeError('Invalid test range ' + test_range)
            start = int(m.group(1))
            end = int(m.group(2) or start)
            tests.extend(range(start, end + 1))
        setattr(namespace, self.dest, tests)


def parse_arguments():

    """ Parse command-line arguments. """

    parser = argparse.ArgumentParser(
        usage='%(prog)s [-h] [options]',
        description='simplesh testing module \{version\}',
        epilog='Example: %(prog)s -i boletin1.json -l 1,3-5,7'
    )

    parser.add_argument(
        '-i', '--in-test-file',
        type=argparse.FileType('r'),
        dest='test_file',
        required=True,
        help='JSON file containing list of tests.')

    parser.add_argument(
        '-t', '--testids',
        type=str,
        dest='testids',
        required=False,
        default=None,
        action=Range2List,
        help='List of ranges of test IDs.')

    parser.add_argument(
        '-d', '--debug',
        dest='debug',
        required=False,
        default=False,
        action='store_true',
        help='Enable debug mode.')

    return parser.parse_args()


################################################################################


class ShStatus(Enum):

    SUCCESS = 0
    FAILURE = 1
    TIMEOUT = 2
    EOFCORE = 3
    NOPRINT = 4
    UNKNOWN = 5


################################################################################


class ShTest:

    """ Shell tests. """

    id = 0

    echo = None
    desc = None
    shell = None
    prompt = None
    timeout = None

    @staticmethod
    def setup(config_d):

        # Initialize class variables
        ShTest.echo = False

        ShTest.desc = config_d.get('desc', 'B0')
        ShTest.shell = config_d.get('shell', 'simplesh')
        ShTest.prompt = config_d.get('prompt', 'simplesh> ')
        ShTest.timeout = config_d.get('timeout', 3)

        # Make sure pexpect can find the shell if it is in the current directory
        os.environ['PATH'] = os.environ.get('PATH', '') + ':' + os.getcwd()

        # TODO: Primitive filesystem sandboxing as chroot requires root privileges

        # Create temporary directory
        try:
            ShTest.cwd = os.getcwd()
            ShTest.tmp_dir = tempfile.TemporaryDirectory()
            os.chdir(ShTest.tmp_dir.name)
        except OSError:
            panic("Error: Unable to create temporary directory: '{}'.".format(ShTest.tmp_dir.name))
        else:
            info("Created temporary directory: '{}'.".format(ShTest.tmp_dir.name))

        # Execute commands
        cmd = None
        try:
            cmds = config_d.get('cmds', '')
            for cmd in cmds:
                subprocess.run(cmd,
                               stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE,
                               check=True, shell=True)
        except OSError:
            panic("Error: Setup command not found: '{}'.".format(cmd))
        except subprocess.CalledProcessError:
            panic("Error: Setup command failed: '{}'.".format(cmd))
        else:
            info("Successfully executed setup commands '{}'.".format(cmds))

    def __init__(self, test_d, config_d):

        if not hasattr(ShTest, 'id'):
            panic("Error: Call ShTest.setup!")

        if not ShTest.id:
            ShTest.setup(config_d)
        ShTest.id += 1

        # Initialize instance variables
        self.id = ShTest.id

        self.cmd = test_d.get('cmd', '')
        self.out = test_d.get('out', '')

        self.shproc = None
        self.status = ShStatus.UNKNOWN
        self.result = ''

    def run(self):

        # Execute shell
        try:
            self.shproc = pexpect.spawn(ShTest.shell,
                                        echo=ShTest.echo,
                                        timeout=ShTest.timeout)
        except pexpect.exceptions.ExceptionPexpect as e:
            panic("Test {:2}: Error executing shell: {}".format(self.id, e))

        # Wait for prompt, execute command and wait for prompt again
        try:
            idx = self.shproc.expect([ShTest.prompt])
            assert(idx == 0)

            self.shproc.sendline(self.cmd)

            idx = self.shproc.expect([ShTest.prompt])
            assert(idx == 0)
        # Prompt not found
        except pexpect.exceptions.TIMEOUT:
            assert(self.shproc.isalive())
            self.status = ShStatus.TIMEOUT
        # Shell process finished or died
        except pexpect.exceptions.EOF:
            assert(not self.shproc.isalive())
            if not self.shproc.status:  # simplesh called exit(0)
                try:
                    self.result = (self.shproc.before.decode('utf-8'))
                except UnicodeDecodeError:
                    self.status = ShStatus.NOPRINT
                else:
                    self.status = ShStatus.SUCCESS if re.match(self.out, self.result) else ShStatus.FAILURE
            else:  # simplesh crashed
                self.status = ShStatus.EOFCORE
        # Prompt found: retrieve command output
        else:
            assert(self.shproc.isalive())
            try:
                self.result = self.shproc.before.decode('utf-8')
            except UnicodeDecodeError:
                self.status = ShStatus.NOPRINT
            else:
                self.status = ShStatus.SUCCESS if re.match(self.out, self.result) else ShStatus.FAILURE
        finally:
            # Terminate process
            if self.shproc.isalive():
                self.shproc.close(force=True)  # Try to terminate process with SIGHUP, SIGINT or SIGKILL
        # Notes:
        #   - https://docs.python.org/3.6.5/library/re.html
        #   - https://pexpect.readthedocs.io/en/stable/overview.html#find-the-end-of-line-cr-lf-conventions

    def print(self, debug=False):

        if self.status == ShStatus.UNKNOWN:
            panic("Test {:2}: Call self.run!".format(self.id))

        if debug:
            print()
        header = "{}: {}.T{:02}: ".format(os.path.basename(sys.argv[0]), ShTest.desc, self.id)
        print(header, end='')

        if self.status == ShStatus.SUCCESS:
            print("Result   : OK!")
        else:
            print("Result   : KO!")

        if debug:
            print(header, end='')
            print("Command  : '{:60}'".format(self.cmd[:60]))
            if self.status == ShStatus.SUCCESS or self.status == ShStatus.FAILURE:
                print(header, end='')
                print("Expected : '{:60}'".format(self.out[:60]))
                print(header, end='')
                print("Produced : '{:60}'".format(self.result[:60]))
                print("{}: {}.T{:02}: Produced: ".format(os.path.basename(sys.argv[0]), ShTest.desc, self.id), end='')
                print('{}'.format(list(self.result)))
            elif self.status == ShStatus.TIMEOUT:
                print(header, end='')
                print("Produced : '{:^60}'".format('TIMEOUT! Prompt not found!'))
            elif self.status == ShStatus.EOFCORE:  # Test with 'raise(SIGSEGV);'
                print(header, end='')
                print("Produced : '{:^60}'".format('CORE! (ulimit -c unlimited)'))
            elif self.status == ShStatus.NOPRINT:  # Test with 'printf("\U3451F50E");'
                print(header, end='')
                print("Produced : '{:^60}'".format('Non-printable charactes in output (possibly a memory leak)'))


################################################################################


def main():

    """ Main driver. """

    info("Version: {}".format(version))

    # Parse command-line arguments
    args = parse_arguments()

    # Parse JSON file
    tests_json = None
    try:
        tests_json = json.load(args.test_file)
    except ValueError:
        panic("Error: Invalid JSON format.".format(args.test_file.name))

    # Instantiate test objects
    tests = [ShTest(t, tests_json['setup']) for t in tests_json['tests']]

    # Run tests
    if args.testids is None:
        for test in tests:
            test.run()
            test.print(debug=args.debug)
    else:
        if not set(args.testids) < set(range(1, len(tests)+1, 1)):
            panic("Error: Invalid list or ranges of test IDs ({}).".format(args.testids))

        for testid in args.testids:
            tests[testid-1].run()
            tests[testid-1].print(debug=args.debug)

    return 0


################################################################################


if __name__ == "__main__":
    sys.exit(main())
