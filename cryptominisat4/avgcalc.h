/*
 * CryptoMiniSat
 *
 * Copyright (c) 2009-2014, Mate Soos. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.0 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301  USA
*/

#ifndef __AVGCALC_H__
#define __AVGCALC_H__

#include "constants.h"
#include <limits>
#include "assert.h"
#include <vector>
#include <cstring>
#include <sstream>
#include <iomanip>

namespace CMSat {
using std::vector;

template <class T, class T2 = uint64_t>
class AvgCalc {
    T2      sum;
    size_t  num;
    #ifdef STATS_NEEDED
    double  sumSqare;
    T       min;
    T       max;
    #endif

public:
    AvgCalc(void) :
        sum(0)
        , num(0)
        #ifdef STATS_NEEDED
        , sumSqare(0)
        , min(std::numeric_limits<T>::max())
        , max(std::numeric_limits<T>::min())
        #endif
    {}

    void push(const T x) {
        sum += x;
        num++;

        #ifdef STATS_NEEDED
        sumSqare += (double)x*(double)x;
        max = std::max(max, x);
        min = std::min(min, x);
        #endif
    }

    #ifdef STATS_NEEDED
    T getMin() const
    {
        return min;
    }

    T getMax() const
    {
        return max;
    }
    double var() const
    {
        if (num == 0)
            return 0;

        const double calcAvg = avg();
        return
            (((double)sumSqare
                - 2.0*calcAvg*(double)sum
            ))/(double)num
             + calcAvg*calcAvg;
    }
    #endif

    double avg() const
    {
        if (num == 0)
            return 0;

        return (double)sum/(double)num;
    }

    std::string avgPrint(size_t prec, size_t w) const
    {
        std::stringstream ss;
        if (num > 0) {
            ss << std::fixed << std::setprecision(prec) << std::setw(w) << std::left
            << avg();
        } else {
            ss << std::setw(5) << "?";
        }

        return ss.str();
    }

    void clear()
    {
        AvgCalc<T, T2> tmp;
        *this = tmp;
    }

    void addData(const AvgCalc& other)
    {
        sum += other.sum;
        num += other.num;

        #ifdef STATS_NEEDED
        sumSqare += other.sumSqare;
        min = std::min(min, other.min);
        max = std::max(max, other.max);
        #endif
    }

    size_t num_data_elements() const
    {
        return num;
    }
};

} //end namespace

#endif //__AVGCALC_H__
