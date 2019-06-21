#! /usr/bin/env /usr/bin/python3

import subprocess
from time import sleep
import shutil
import os
import numpy
import json

class channel_benchmark():
    def __init__(self, tests, runsPerTest=10, timeBetweenRuns=1,
                 senderCore=0, readerCore=2,
                 channelArgs=["interval", "primeTime", "accessTime"]):

        self.sender = ['taskset', '-c', str(senderCore),
                       sender_bin, "-b"]
        self.resultFile = os.path.join(result_dir, "llc-pp.json")
        try:
            os.mkdir(result_dir)
        except (OSError):
            pass

        self.reader = ['taskset', '-c', str(readerCore),
                       reader_bin, "-b"]

        self.cool_down = timeBetweenRuns
        self.channelArgs = channelArgs
        self.runs = runsPerTest
        self.tests = tests

    def readChannelFile(self, f):
        if os.path.isfile(f):
            with open(f) as fd:
                return [int(datum.split(" ")[1]) for datum in fd.readlines()]
        else:
            return []

    def check(self, sender, reader):
        readerData = self.readChannelFile(reader)
        senderData = self.readChannelFile(sender)
        transition = numpy.zeros((2, 2), dtype=numpy.float64)
        if (len(senderData) != len(readerData)) :
            print("Data length mismatch!")
        elif (len(senderData) == 0) :
            print("No Data!")
        else:
            for sent,read in zip(senderData, readerData):
                transition[sent, read] += 1
            print("Printing bit transition matrix as:")
            print("[[0 -> 0, 0 -> 1],\n [1 -> 0, 0 -> 1]]\n")
            print(transition)
        return transition

    def capacity(self, transition):
        m,n = transition.shape
        r = numpy.ones((m,)) / m
        q = numpy.zeros((m,n))
        error_allowed = 1e-5/m
        for i in range(n):
            if transition[:,i].sum() == 0:
                print("error: transition matrix contains a zero columnn")
                return 0

        for i in range(m):
            if transition[i].sum() == 0:
                print("error: transition matrix contains a zero row")
                return 0
            transition[i] /= transition[i].sum()

        for iter in range(10000):
            for i in range(n):
                q[:,i] = r * transition[:,i]
                q[:,i] /= q[:,i].sum()
            rprime = r.copy()
            for i in range(m):
                rprime[i] = (q[i]**transition[i]).prod()
            rprime = rprime/rprime.sum()
            if (numpy.linalg.norm(rprime - r)) < error_allowed:
                break
            else:
                r = rprime

        cap = 0
        for i in range(m):
            for j in range(n):
                if (r[i] > 0 and q[i,j] > 0):
                    cap += r[i]*transition[i,j]*numpy.log2(q[i,j]/r[i])
        return cap


    def doTest(self, paramMap):
        print("\n==== Test parameters:{} ====".format(paramMap))
        try:
            shutil.rmtree(data_dir)
        except (OSError):
            pass
        try:
            os.mkdir(data_dir)
        except (OSError):
            pass
        contents = {}
        if not os.path.isfile(self.resultFile) \
           or os.path.getmtime(sender_bin) < os.path.getmtime(self.resultFile):
            try:
                with open(self.resultFile) as results:
                    contents = json.load(results)
            except OSError:
                pass
            except Exception:
                print("Warning: corrupted results file")

        key = json.dumps([paramMap[arg] for arg in self.channelArgs])
        if (key in contents) and (len(contents[key]) >= 3):
            # estimate
            bitsPerSec = 2300 * 1000 * 1000 / paramMap["interval"]
            print("  Capacity: {}".format(max(contents[key])))
            print("  Bandwidth: {}".format(max(contents[key]) * bitsPerSec))
            return max(contents[key]) * bitsPerSec

        for i in range(self.runs):
            print("  run #{}...".format(i), end='')
            readerOut = os.path.join(data_dir, "receiverSave")
            senderOut = os.path.join(data_dir, "senderSave")
            reader = subprocess.Popen(self.reader
                                      + ['-i', str(paramMap['interval'])]
                                      + ['-p', str(paramMap['primeTime'])]
                                      + ['-a', str(paramMap['accessTime'])]
                                      ,stdin=subprocess.PIPE
                                      ,stdout=subprocess.PIPE
                                      # ,cwd=run_dir
                                      ,env=env)
            sender = subprocess.Popen(self.sender
                                      + ['-i', str(paramMap['interval'])]
                                      + ['-p', str(paramMap['primeTime'])]
                                      + ['-a', str(paramMap['accessTime'])]
                                      ,stdin=subprocess.PIPE
                                      ,stdout=subprocess.PIPE
                                      # ,cwd=run_dir
                                      ,env=env)
            sleep(self.cool_down)
            sender.wait()
            sleep(self.cool_down)
            # reader.wait() # needs work
            # in case reader did not get all rounds of testing from sender
            if reader.poll is None:
                reader.kill()
                print("Reader faild to get all benchmarking messages\n");
                return 0
            print("Done")

            reader_stdout = reader.communicate()[0].decode().split(' ')
            bits = int(reader_stdout[-4])
            nsec = int(reader_stdout[-1])

            if paramMap["interval"] != 0:
                # bitsPerSec_no_sync = 2300 * 1000 * 1000 / paramMap["interval"]
                bitsPerSec = bits * 1e9 / nsec
            else:
                eitsPerSec = 0

            if (key not in contents) :
                contents[key] = []
            cap = float(self.capacity(self.check(senderOut, readerOut)))
            print("  Capacity: {}".format(cap))
            print("  Bandwidth: {}".format(cap * bitsPerSec))
            contents[key] += [cap]
        with open(self.resultFile, "w+") as results:
            json.dump(contents,results)
            results.flush()
        return max(contents[key]) * bitsPerSec

    def benchmark(self):
        for test in self.tests:
            self.doTest(test)

if __name__ == '__main__':
    data = map(
        lambda s: {"interval":s[0], "primeTime":s[1], "accessTime":s[2]},
        [# interval, primeTime, accessTime
         (2000000, 800000, 800000),
         (1000000, 400000, 400000)
         ][0:2])

    base_dir = os.path.dirname(os.path.abspath(__file__))
    data_dir = os.path.join(base_dir , "data")
    result_dir = os.path.join(base_dir, "llc-results")

    sender_bin = os.path.join(base_dir, "pp-llc-send")
    reader_bin = os.path.join(base_dir, "pp-llc-recv")

    if not os.path.isfile(sender_bin) or not os.path.isfile(reader_bin):
        print("Please have this script in the same folder as the executables")
        exit()

    env = os.environ.copy()
    try :
        env["LD_LIBRARY_PATH"] += ":" + base_dir
    except KeyError:
        env["LD_LIBRARY_PATH"] = base_dir

    channel = channel_benchmark(data
                                ,runsPerTest=1
                                ,readerCore=0
                                ,senderCore=2)
    channel.benchmark();

