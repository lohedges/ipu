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

#include <poplar/Vertex.hpp>

class AddSomething : public poplar::Vertex
{
public:
    // Fields.
    poplar::Input<int> something;
    poplar::InOut<int> input_output;

    // Compute method.
    bool compute()
    {
        *input_output = input_output + something;

        // All okay!
        return true;
    }
};
