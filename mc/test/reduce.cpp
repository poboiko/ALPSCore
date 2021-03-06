/*
 * Copyright (C) 1998-2014 ALPS Collaboration. See COPYRIGHT.TXT
 * All rights reserved. Use is subject to license terms. See LICENSE.TXT
 * For use in publications, see ACKNOWLEDGE.TXT
 */

#include <alps/mc/mcbase.hpp>
#include <alps/mc/mpiadapter.hpp>
#include <alps/mc/api.hpp>
#include <alps/mc/stop_callback.hpp>

#include <boost/mpi.hpp>

#include <alps/utilities/boost_mpi.hpp>
#include <alps/accumulators.hpp>
#include <alps/params.hpp>
 
#include "gtest/gtest.h"

class sim1 : public alps::mcbase {
    public:
        
        sim1(parameters_type const & p, std::size_t seed_offset = 0):
            alps::mcbase(p, seed_offset),
            nsweeps(p["nsweeps"]), 
            count(0) 
        {
            measurements << alps::accumulators::FullBinningAccumulator<std::vector<double> >("e1");
        }
 
        void update() { 
            count++; 
        }

        void measure() { 
            measurements["e1"] << std::vector<double>(5, 1.0);
        }

        double fraction_completed() const { return double (count/nsweeps); }
 
    private:
        int nsweeps;
        int count;
};

int main() {
// TEST(mc, reduce){
    boost::mpi::environment env;
    boost::mpi::communicator comm;
    alps::params p;
    p["nsweeps"] = 10000;
    alps::mcmpiadapter<sim1> sim (p,comm, alps::check_schedule(0.001, 60));
    sim.run(alps::stop_callback(1));
    if (comm.rank() == 0) {
        alps::results_type<sim1>::type res = alps::collect_results(sim);
        std::cout << res << std::endl;
    } else
        alps::collect_results(sim);
}