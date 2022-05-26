/*
  Copyright (c) 2022 Lester Hedges <lester.hedges@gmail.com>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include <chrono>
#include <cmath>
#include <exception>
#include <iostream>
#include <vector>

#include <poplar/DeviceManager.hpp>
#include <poplar/Engine.hpp>
#include <poplar/Graph.hpp>
#include <poplar/IPUModel.hpp>

#include <poputil/TileMapping.hpp>

// Handy enum to name our programs.
enum Program
{
    COPY_TO_IPU,
    ADD_SOMETHING,
    MULTIPLY_SOMETHING_NUM_TIMES,
    SUM,
    COPY_FROM_IPU
};

// Connect to a device with the requested number of IPUs.
poplar::Device setIpuDevice(unsigned num_ipus);

// Compute the time in milliseconds relative to a starting point.
double timeIt(const std::chrono::time_point<std::chrono::steady_clock> &start);

int main(int argc, char *argv[])
{
    // Specify the number of IPUs and tiles per IPU to use.
    // These can be overriden from the command-line.
    unsigned num_ipus = 1;
    unsigned num_tiles_per_ipu = 2;

    // Rudimentary command-line argument parsing.

    // Get the number of IPUs.
    if (argc > 1)
    {
        std::string s(argv[1]);

        try
        {
            std::size_t pos;
            num_ipus = std::stoi(s, &pos);
            if (pos < s.size())
            {
                std::cerr << "Trailing characters after number of IPUs: " << s << '\n';
                exit(-1);
            }
        }
        catch (std::invalid_argument const &ex)
        {
            std::cerr << "Invalid number of IPUs: " << s << '\n';
            exit(-1);
        }
        catch (std::out_of_range const &ex)
        {
            std::cerr << "Number of IPUs out of range: " << s << '\n';
            exit(-1);
        }

        // Check against hardcoded limits. Can query device to see what's available.
        if ((num_ipus < 1) or (num_ipus > 4))
        {
            std::cerr << "Number of IPUs must be between 1 and 4!\n";
            exit(-1);
        }
    }
    // Get the number of tiles per IPU.
    if (argc > 2)
    {
        std::string s(argv[2]);

        try
        {
            std::size_t pos;
            num_tiles_per_ipu = std::stoi(s, &pos);
            if (pos < s.size())
            {
                std::cerr << "Trailing characters after number of tiles: " << s << '\n';
                exit(-1);
            }
        }
        catch (std::invalid_argument const &ex)
        {
            std::cerr << "Invalid number of tiles: " << s << '\n';
            exit(-1);
        }
        catch (std::out_of_range const &ex)
        {
            std::cerr << "Number of tiles out of range: " << s << '\n';
            exit(-1);
        }

        // Check against hardcoded limits. Can query device to see what's available.
        if ((num_tiles_per_ipu < 1) or (num_tiles_per_ipu > 1472))
        {
            std::cerr << "Number of tiles per IPU must be between 1 and 1472!\n";
            exit(-1);
        }
    }

    poplar::Device device;

    // Try to connect to a device with the requested number of IPUs.
    try
    {
        device = setIpuDevice(num_ipus);

        std::string ipu_string = (num_ipus > 1) ? "IPUs" : "IPU";
        std::cout << "Using " << num_ipus << " " << ipu_string
                  << " and " << num_tiles_per_ipu << " tiles per IPU.\n";
    }
    // Use an IPUModel as a fallback.
    catch(...)
    {
        std::string ipu_string = (num_ipus > 1) ? "IPUs" : "IPU";
        std::cout << "Unable to connect to a device with "
                  << num_ipus << " " << ipu_string << ".\n";
        std::cout << "Using an IPUModel with 1 IPU and "
                  << num_tiles_per_ipu << " tiles per IPU.\n";
        std::cout << "Ignore timing statistics.\n";

        num_ipus = 1;
        poplar::IPUModel ipuModel;
        device = ipuModel.createDevice();
    }

    // Store the number of hardware workers per tile. We'll make use of all
    // threads.
    const unsigned num_workers = device.getTarget().getNumWorkerContexts();

    // Store the total number of tiles.
    const unsigned num_tiles = num_ipus * num_tiles_per_ipu;

    // Create a Graph object.
    poplar::Graph graph(device);

    // Add codelets.
    graph.addCodelets({"src/AddSomethingCodelet.cpp",
                       "src/MultiplySomethingNumTimesCodelet.cpp",
                       "src/SumCodelet.cpp"},
                        "-O3");

    // Work out the size of our tensors. (For simplicity, we'll have one element
    // for each worker on each tile.)
    const unsigned num_workers_total = num_ipus * num_tiles_per_ipu * num_workers;

    // Add constants and variables to the graph.

    // Add a couple of constants.
    const auto five = graph.addConstant<int>(poplar::INT, {}, 5);
    const auto ten  = graph.addConstant<int>(poplar::INT, {}, 10);

    // Add tensors. These will hold the input and output of our codelets.
    // The first tensor is used for single-valued input/output.
    const auto tensor0 = graph.addVariable(
            poplar::INT,
            {num_workers_total},
            "tensor0");
    // Add a second, two-dimensional tensor with num_workers rows and 20 columns.
    // This is used for multi-valued input/output.
    const auto tensor1 = graph.addVariable(
            poplar::INT,
            {num_workers_total, 20},
            "tensor1");

    // Map the constants to the first tile.
    graph.setTileMapping(five, 0);
    graph.setTileMapping(ten, 0);

    // Map the tensors linearly to the tiles, i.e. spreading the elements
    // evenly amongst the tiles. Note that Poplar tensors are row-major,
    // so the mapping would't work correctly if your data was ordered in
    // a column-major fashion.
    poputil::mapTensorLinearly(graph, tensor0);
    poputil::mapTensorLinearly(graph, tensor1);

    /* Could also do this manually, as shown below.
    for (unsigned i=0; i<num_tiles; ++i)
    {
        // Map num_workers elements of tensor0 to the tile.
        graph.setTileMapping(tensor0.slice(num_workers*i, num_workers*(i+1)), i);

        // Map 20 columns of num_workers elements from tensor1 to the tile.
        graph.setTileMapping(tensor1.slice({num_workers*i, 0}, {num_workers*(i+1), 20}), i);
    }*/

    // Create three compute sets to run our "algorithms".
    poplar::ComputeSet computeSet0 = graph.addComputeSet("computeSet0");
    poplar::ComputeSet computeSet1 = graph.addComputeSet("computeSet1");
    poplar::ComputeSet computeSet2 = graph.addComputeSet("computeSet2");

    // Add vertices to each compute set.
    for (unsigned i=0; i<num_workers_total; ++i)
    {
        // Create a vertex for each codelet.
        poplar::VertexRef vtx0 = graph.addVertex(computeSet0, "AddSomething");
        poplar::VertexRef vtx1 = graph.addVertex(computeSet1, "MultiplySomethingNumTimes");
        poplar::VertexRef vtx2 = graph.addVertex(computeSet2, "Sum");

        // Connect vertex inputs and outputs to the appropriate tensors.

        // Add.
        graph.connect(vtx0["something"], five);
        graph.connect(vtx0["input_output"], tensor0[i]);

        // Repeat multiply.
        // (Take slice of 2D tensor1 and flatten to a 1D tensor.)
        graph.connect(vtx1["something"], ten);
        graph.connect(vtx1["input"],  tensor0[i]);
        graph.connect(vtx1["output"], tensor1.slice({i, 0}, {i+1, 20}).flatten());

        // Sum.
        // (Take slice of 2D tensor1 and flatten to a 1D tensor.)
        graph.connect(vtx2["input"], tensor1.slice({i, 0}, {i+1, 20}).flatten());
        graph.connect(vtx2["output"], tensor0[i]);

        // Work out the tile index.
        const auto tile = std::floor(i / num_workers);

        // Map the vertices to the tile.
        graph.setTileMapping(vtx0, tile);
        graph.setTileMapping(vtx1, tile);
        graph.setTileMapping(vtx2, tile);

        // Add some crude performance estimates.
        // (These are only required if running on an IPUModel.)
        graph.setPerfEstimate(vtx0, 1);
        graph.setPerfEstimate(vtx1, 120);
        graph.setPerfEstimate(vtx2, 20);
    }

    // Create a vector to store our programs.
    std::vector<poplar::program::Program> programs;

    // Create host-to-IPU data stream and associated copy program.
    auto input_write = graph.addHostToDeviceFIFO(
            "input_write",
            poplar::INT,
            num_workers_total);
    auto copy_input = poplar::program::Copy(input_write, tensor0);

    // Create IPU-to-host data stream and associated copy program.
    auto output_read = graph.addDeviceToHostFIFO(
            "output_read",
            poplar::INT,
            num_workers_total);
    auto copy_output = poplar::program::Copy(tensor0, output_read);

    // Add the host-to-IPU copy program.
    programs.push_back(copy_input);

    // Create a program to repeat the addition 100 times.
    auto add_sequence = poplar::program::Sequence
    {
        poplar::program::Repeat(
            100,
            poplar::program::Execute(computeSet0)
        ),
    };

    // Add the compute sets for our "algorithms".
    programs.push_back(add_sequence);
    programs.push_back(poplar::program::Execute(computeSet1));
    programs.push_back(poplar::program::Execute(computeSet2));

    // Add the IPU-to-host copy program.
    programs.push_back(copy_output);

    // Create a buffers to hold our input/output, zeroing the input buffer.
    std::vector<int> buffer_in(num_workers_total);
    std::vector<int> buffer_out(num_workers_total);
    std::fill(buffer_in.begin(), buffer_in.end(), 0);

    // Record start time.
    auto start = std::chrono::steady_clock::now();

    // Compile the graph program.
    std::cout << "\nCompiling graph program...\n";
    poplar::Engine engine(graph, programs);
    std::cout << "  Took " << timeIt(start) << " ms\n";

    // Load the program on the device.
    std::cout << "Loading program on device...\n";
    start = std::chrono::steady_clock::now();
    engine.load(device);
    std::cout << "  Took " << timeIt(start) << " ms\n";

    // Connect input/output data stream.
    engine.connectStream("input_write", buffer_in.data());
    engine.connectStream("output_read", buffer_out.data());

    // Run the host-to-IPU data stream copy.
    std::cout << "Copying input data to IPU...\n";
    start = std::chrono::steady_clock::now();
    engine.run(Program::COPY_TO_IPU);
    std::cout << "  Took " << timeIt(start) << " ms\n";

    // Run add program.
    std::cout << "Running repeat add program...\n";
    start = std::chrono::steady_clock::now();
    engine.run(Program::ADD_SOMETHING);
    std::cout << "  Took " << timeIt(start) << " ms\n";

    // Run multiply program.
    std::cout << "Running multiply / clone program...\n";
    start = std::chrono::steady_clock::now();
    engine.run(Program::MULTIPLY_SOMETHING_NUM_TIMES);
    std::cout << "  Took " << timeIt(start) << " ms\n";

    // Run sum program.
    std::cout << "Running sum program...\n";
    start = std::chrono::steady_clock::now();
    engine.run(Program::SUM);
    std::cout << "  Took " << timeIt(start) << " ms\n";

    // Run the IPU-to-host data stream copy.
    std::cout << "Copying ouput data from IPU...\n";
    start = std::chrono::steady_clock::now();
    engine.run(Program::COPY_FROM_IPU);
    std::cout << "  Took " << timeIt(start) << " ms\n";

    // Loop over the output buffer to validate the output.
    std::cout << "Validating output...\n";
    for (unsigned i=0; i<buffer_out.size(); ++i)
    {
        // Each value should be 5*100*10*20 = 100000.
        assert(buffer_out[i] == 100000);
    }

    std::cout << "Done!\n";

    return 0;
}

poplar::Device setIpuDevice(unsigned num_ipus)
{
    auto dm = poplar::DeviceManager::createDeviceManager();
    auto hwDevices = dm.getDevices(poplar::TargetType::IPU, num_ipus);
    if (hwDevices.size() > 0)
    {
        for (auto &d : hwDevices)
        {
            if (d.attach())
            {
                return std::move(d);
            }
        }
    }

    throw std::runtime_error("Unable to connect to IPU device!");
}

double timeIt(const std::chrono::time_point<std::chrono::steady_clock> &start)
{
    // Record current time point and work out duration.
    auto finish = std::chrono::steady_clock::now();

    // Return duration in milliseconds.
    return std::chrono::duration<double, std::milli>(finish - start).count();
}
