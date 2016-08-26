/**
 * @file particle_goal.hpp
 * 
 * @copyright Software License Agreement (BSD License)
 * Copyright (c) 2013, Rutgers the State University of New Jersey, New Brunswick  
 * All Rights Reserved.
 * For a full description see the file named LICENSE.
 * 
 * Authors: Andrew Dobson, Andrew Kimmel, Athanasios Krontiris, Zakary Littlefield, Kostas Bekris 
 * 
 * Email: pracsys@googlegroups.com
 */
#pragma once

#ifndef PRX_PARTICLE_GOAL_REGION_HPP
#define	PRX_PARTICLE_GOAL_REGION_HPP

#include "prx/utilities/definitions/defs.hpp"
#include "prx/utilities/goals/goal.hpp"
#include "utilities/particle_space.hpp"

 namespace prx 
 { 
    namespace util
    {
        class parameter_reader_t;
    }
   namespace packages 
   {
    namespace conformant
    {

         class particle_goal_t : public util::goal_t
         {
            public:
                particle_goal_t();
                virtual ~particle_goal_t();

                    /** @copydoc goal_t::init(const parameter_reader_t*, const parameter_reader_t*) */
                virtual void init(const util::parameter_reader_t* reader, const util::parameter_reader_t* template_reader);

                    /** @copydoc goal_t::link_space( const space_t* ) */
                virtual void link_space(const util::space_t* inspace );

                    /** @copydoc goal_t::satisfied(const space_point_t* ) */
                virtual bool satisfied(const util::space_point_t* state);
                /** @copydoc goal_t::satisfied(const space_point_t* , double* ) */
                virtual bool satisfied(const util::space_point_t* state, double& distance);

                /**
                 * Gets the radius for satisfying states.
                 * @brief Gets the radius for satisfying states.
                 * @return The radius.
                 */
                 double get_radius() const;

                /**
                 * Gets the goal point as a vector of doubles.
                 * @brief Gets the goal point as a vector of doubles.
                 * @return The goal vector.
                 */
                 const std::vector<double>& get_goal_vec() const;

                /**
                 * Sets the goal parameters by argument.
                 * @brief Sets the goal parameters by argument.
                 * @param d_metric The distance metric to use.
                 * @param s_vec The vector corresponding to the center state.
                 * @param r The radius around \c s_vec to consider successful.
                 */
                 void set_goal(util::distance_metric_t* d_metric, const std::vector<double>& s_vec, double r);

                 void link_particle_space(const util::particle_space_t* p_space)
                 {
                    particle_space = p_space;
                 }

             protected:

                double accepted_threshold;

                const util::particle_space_t* particle_space;

                /**
                 * @brief The temporary storage of the center state.
                 */
                std::vector<double> state_vec;
                /**
                 * @brief The space_point representing the center state.
                 */
                util::space_point_t* point;
                /**
                 * @brief The radius representing the successful region.
                 */
                double radius;

                util::space_point_t* test_point;
             };
         }
    } 
}

#endif
