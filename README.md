# Example code for the IPU crash course

This repository contains example code for the IPU crash course.

## Compiling

First clone the repository:

```
git clone https://github.com/lohedges/ipu.git
```

Now navigate to the repository directory and run `make`:

```
cd ipu
make
```

After this, you should find the `ipu_example` executable in the directory.

## Running

To run the example:

```
./ipu_example
```

By default this will run on two tiles of a single IPU. To specify a different
number of resources, run with:

```
./ipu_example 4 1472
```

The arguments specify the number of IPUs and tiles per IPU respectively.
(Note that this is designed for an IPU-POD4, i.e. with 4 IPUs in total,
with 1472 tiles per IPU.)

The example code executes illustrates some basic concepts that are used to
create and run a graph program. The graph program executes a sequence of
three simple _algorithms_, which are implemented as `Vertex` codelets running
on all worker threads on the requested tiles. Starting from a zeroed rank-1
tensor with a size equal to the total number of workers, the algorithms
perform:

* `AddSomething`: Add a constant to an item in the tensor.
* `MultiplySomethingNumTimes`: Multiply an item in the tensor by a constant a number of times, creating `num` outputs for a single input, i.e. duplicating the output `num` times.
* `Sum`: Sum the items in a tensor.

## Output

When run, the program will report timing output for the various steps in the
creating and running of the graph program. You should see something like:

```
Using 4 IPUs and 1472 tiles per IPU.

Compiling graph program...
  Took 5363.51 ms
Loading program on device...
  Took 2024.64 ms
Copying input data to IPU...
  Took 0.578061 ms
Running repeat add program...
  Took 0.956642 ms
Running multiply / clone program...
  Took 0.11782 ms
Running sum program...
  Took 0.11993 ms
Copying ouput data from IPU...
  Took 0.328752 ms
Validating output...
Done!
```
