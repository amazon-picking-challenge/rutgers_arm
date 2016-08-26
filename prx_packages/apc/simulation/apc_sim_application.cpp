/**
 * @file application.cpp
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

#include "prx/utilities/parameters/parameter_reader.hpp"
#include "prx/utilities/definitions/string_manip.hpp"
#include "prx/simulation/systems/plants/plant.hpp"
#include "prx/simulation/collision_checking/vector_collision_list.hpp"
#include "prx/simulation/systems/obstacle.hpp"


#include "../../manipulation/simulation/simulators/manipulation_simulator.hpp"

#include "prx/simulation/communication/sim_base_communication.hpp"
#include "prx/simulation/communication/planning_comm.hpp"
#include "prx/simulation/communication/simulation_comm.hpp"
#include "prx/simulation/communication/visualization_comm.hpp"
#include "prx/utilities/communication/tf_broadcaster.hpp"

#include <boost/range/adaptor/map.hpp> //adaptors
#include <fstream>
#include "prx/utilities/definitions/sys_clock.hpp"

#include "simulation/apc_sim_application.hpp"
#include <pluginlib/class_list_macros.h>
#include <map>

#ifdef PRX_SENSING_FOUND
#include "prx_sensing/UpdateObjectList.h"
#include "prx_sensing/PublishObjectList.h"
#include "prx_sensing/UpdateShelfPosition.h"
#include "prx_sensing/SwitchCameras.h"
#endif

#include "prx_simulation/gripper_change_srv.h"
#include "prx_simulation/UnigripperVacuumOn.h"

 //Json Parser
#include <simulation/json/read_file.h>


 PLUGINLIB_EXPORT_CLASS(prx::packages::apc::apc_sim_application_t, prx::sim::application_t)



 namespace prx
 {
    using namespace util;
    using namespace sim;
    using namespace sim::comm;

    namespace packages
    {
        using namespace manipulation;
        namespace apc
        {
            const std::string dof_names[16] = {"torso_joint_b1", "arm_left_joint_1_s", "arm_left_joint_2_l", "arm_left_joint_3_e", "arm_left_joint_4_u", "arm_left_joint_5_r", "arm_left_joint_6_b", "arm_left_joint_7_t", "arm_right_joint_1_s", "arm_right_joint_2_l", "arm_right_joint_3_e", "arm_right_joint_4_u", "arm_right_joint_5_r", "arm_right_joint_6_b", "arm_right_joint_7_t", "torso_joint_b2"};
            apc_sim_application_t::apc_sim_application_t()
            {
                sem_init(&semaphore, 0, 1);
                tf_broadcaster = NULL;
                simulator_running = false;
                simulator_counter = 0;
                simulator_mode = 0;
                loop_counter = 0;
                loop_total = 0.0;
                new_geometries_info = false;
                selected_point.resize(3);
                selected_path.clear();
                collision_list = new vector_collision_list_t();

                stores_states = replays_states = false;


            }

            apc_sim_application_t::~apc_sim_application_t()
            {
                delete tf_broadcaster;
                delete plan_comm;
                delete sim_comm;
                delete vis_comm;
                sem_destroy(&semaphore);
            }

            void apc_sim_application_t::init(const parameter_reader_t * const reader)
            {
                shelf_detection_done = false;                
                Init_Finished = false;
                PRX_INFO_S("Initializing APC Simulation Application");
                current_item_index = 0;
                number_of_orders = 0;
                current_bin_name = 0;
                Got_Item = false;
                rearrangement_flag = false;
                //getting the subsystems
                automaton_state = START;
                displayed_state = false;
                execution_time =0.0;
                plan_duration = 0.0;
                currently_in_home_position = true;
                executing_trajectory = false;
                counter = 0;
                planning_app_query = new planning_app_query_client("prx/apc_action",false);
                controller_query = new send_traj_client("prx/execute_plan_action",false);

                simulation::update_all_configs = true;
                std::string plugin_type_name;
                plugin_type_name = reader->get_attribute("sim_to_plan_comm", "planning_comm");
                comm::plan_comm = sim_base_communication_t::get_loader().createUnmanagedInstance("prx_simulation/" + plugin_type_name);
                plan_comm->link_application(this);

                plugin_type_name = reader->get_attribute("sim_comm", "simulation_comm");
                sim_comm = sim_base_communication_t::get_loader().createUnmanagedInstance("prx_simulation/" + plugin_type_name);
                sim_comm->link_application(this);


                PRX_PRINT("Initializing the application", PRX_TEXT_GREEN);
                simulator = reader->initialize_from_loader<simulator_t > ("simulator", "prx_simulation");
                simulator->update_system_graph(sys_graph);
                sys_graph.get_name_system_hash(subsystems);
// #ifdef PRX_SENSING_FOUND
                PRX_PRINT("Found PRX Sensing...",PRX_TEXT_GREEN);
                // Testing calling services advertised by prx_sensing
                ros::ServiceClient prx_sensing_update_cl = n.serviceClient<prx_sensing::UpdateObjectList>("prx/sensing/update_object_list");

                prx_sensing::UpdateObjectList sensing_update_srv;
                sensing_update_srv.request.object_list.push_back("ticonderoga_12_pencils");
                sensing_update_srv.request.object_list.push_back("crayola_24_ct");
                sensing_update_srv.request.object_list.push_back("expo_dry_erase_board_eraser");
                sensing_update_srv.request.object_list.push_back("i_am_a_bunny_book");
                sensing_update_srv.request.object_list.push_back("dove_beauty_bar");
                sensing_update_srv.request.object_list.push_back("soft_white_lightbulb");
                sensing_update_srv.request.object_list.push_back("kleenex_tissue_box");
                sensing_update_srv.request.object_list.push_back("laugh_out_loud_joke_book");
                sensing_update_srv.request.object_list.push_back("staples_index_cards");


                prx_sensing_update_cl.call(sensing_update_srv);
                PRX_INFO_S(sensing_update_srv.response);

                moved = NULL;
// #endif   
                foreach(system_ptr_t plant, subsystems | boost::adaptors::map_values)
                {
                    // PRX_PRINT("plants iteration",PRX_TEXT_GREEN);
                    //Create and initialize information for all the end effectors. 
                    if(dynamic_cast<manipulator_t*>(plant.get()) != NULL)
                    {
                        //Create end effector infos for each end effector.
                        manipulator = dynamic_cast<manipulator_t*>(plant.get());
                    }

                    //Create a list of all the movable bodies.
                    if(dynamic_cast<movable_body_plant_t*>(plant.get()) != NULL)
                    {
                        movable_bodies.push_back(dynamic_cast<movable_body_plant_t*>(plant.get()));
                    }

                    //Create a controller
                    if ( dynamic_cast<apc_controller_t*> (plant.get()) != NULL )
                    {
                     PRX_INFO_S(" Found a controller!");
                     controller = dynamic_cast<apc_controller_t*> (plant.get());
                 }
             }

             has_dynamic_obstacles = reader->get_attribute_as<bool>("application/has_dynamic_obstacles", true);
             visualize = reader->get_attribute_as<bool>("application/visualize", true);
             screenshot_every_simstep = reader->get_attribute_as<bool>("application/screenshot_every_simstep", false);
             simulator_running = reader->get_attribute_as<bool>("application/start_simulation",false);
             offset_threshold = reader->get_attribute_as<double>("application/offset_threshold",0.04);

             simulation_control_space = simulator->get_control_space();
             simulation_state_space = simulator->get_state_space();
             simulation_state = simulation_state_space->alloc_point();
             simulation_control = simulation_control_space->alloc_point();

             deserialize_sim_file = reader->get_attribute_as<std::string > ("application/deserialize_sim_file", "");
             if( !deserialize_sim_file.empty() )
             {
                this->replays_states = true;
                replay_simulation_states.link_space(simulation_state_space);
                deserialize_simulation_states(deserialize_sim_file, replay_simulation_states);
            }

            // Object priorities

            const parameter_reader_t* child_template_reader = NULL;

            if( parameters::has_attribute("template", reader, NULL) )
            {
                std::string template_name = parameters::get_attribute("template", reader, NULL);
                child_template_reader = new parameter_reader_t(ros::this_node::getName() + "/" + template_name, global_storage);
            }

            if (parameters::has_attribute("object_to_prioritized_end_effector_context", reader, child_template_reader))
            {
                PRX_PRINT ("\n\n\nCHECK\n\n\n", PRX_TEXT_GREEN);


                parameter_reader_t::reader_map_t map = parameters::get_map("object_to_prioritized_end_effector_context",reader,child_template_reader);
                foreach(const parameter_reader_t::reader_map_t::value_type key_value, map)
                {                    
                    std::vector<std::string> prioritized_context = key_value.second->get_attribute_as< std::vector<std::string> >("");
                    object_to_prioritized_end_effector_context[key_value.first] = prioritized_context;

                    // DEBUG PRINT
                    PRX_DEBUG_COLOR("Object name: " << key_value.first, PRX_TEXT_MAGENTA);
                    for (auto name : object_to_prioritized_end_effector_context[key_value.first] )
                    {
                        PRX_DEBUG_COLOR("EE Context name: " << name, PRX_TEXT_CYAN);
                    } 
                }
            }
            else
            {
                PRX_FATAL_S ("Need to define the end effector contexts to use for each object!");
            }



            if( parameters::has_attribute("pose_template", reader, NULL) )
            {
                std::string template_name = parameters::get_attribute("pose_template", reader, NULL);
                child_template_reader = new parameter_reader_t(ros::this_node::getName() + "/" + template_name, global_storage);
            }
            if (parameters::has_attribute("object_poses", reader, child_template_reader))
            {
                PRX_DEBUG_COLOR ("\n\n\nCHECK\n\n\n", PRX_TEXT_GREEN);

                parameter_reader_t::reader_map_t map = parameters::get_map("object_poses",reader,child_template_reader);
                foreach(const parameter_reader_t::reader_map_t::value_type key_value, map)
                {                    
                    std::vector<double> object_pose = key_value.second->get_attribute_as< std::vector<double> >("");
                    object_poses[key_value.first] = object_pose;

                    // DEBUG PRINT
                    PRX_DEBUG_COLOR("Object name: " << key_value.first, PRX_TEXT_MAGENTA);
                    for (auto name : object_poses[key_value.first] )
                    {
                        PRX_DEBUG_COLOR("Object pose: " << name, PRX_TEXT_CYAN);
                    } 
                }
            }

            if( parameters::has_attribute("json_file", reader, NULL) )
            {
                std::string template_name = parameters::get_attribute("json_file", reader, NULL);
                child_template_reader = new parameter_reader_t(ros::this_node::getName() + "/" + template_name, global_storage);
            }

            if( parameters::has_attribute("json_file", reader, child_template_reader) )
                json_file = parameters::get_attribute_as<std::string>("json_file", reader, child_template_reader, "json_file");

            // PRX_FATAL_S ("NO!");




            serialize_sim_file = reader->get_attribute_as<std::string > ("application/serialize_sim_file", "");
            if( !serialize_sim_file.empty() )
            {
                store_simulation_states.link_space(simulation_state_space);
                this->stores_states = true;
            }

            PRX_ASSERT(simulator != NULL);
                //get obstacles
            obstacles_hash = simulator->get_obstacles();
            foreach(system_ptr_t obstacle, obstacles_hash | boost::adaptors::map_values)
            {
                if(dynamic_cast<sim::obstacle_t*>(obstacle.get()) != NULL)
                {
                    obstacles.push_back(dynamic_cast<sim::obstacle_t*>(obstacle.get()));
                }
            }
            tf_broadcaster = new tf_broadcaster_t;

            sim_key_name = "prx/input_keys/" + int_to_str(PRX_KEY_RETURN);
            if( !ros::param::has(sim_key_name) )
                ros::param::set(sim_key_name, false);

            simulator->update_system_graph(sys_graph);

            sys_graph.get_plant_paths(plant_paths);
            if( visualize )
            {
                plugin_type_name = reader->get_attribute("sim_to_vis_comm", "visualization_comm");
                vis_comm = sim_base_communication_t::get_loader().createUnmanagedInstance("prx_simulation/" + plugin_type_name);
                vis_comm->link_application(this);

                vis_comm->send_plants(ros::this_node::getName(), plant_paths);

                    //NEW SHELF VISUALIZATION
                ((visualization_comm_t*)vis_comm)->send_rigid_bodies();

                    //OLD SHELF VISUALIZATION
                    // if(ros::param::has("prx/obstacles"))
                    //     vis_comm->visualize_obstacles("prx");    
                    // else
                    //     vis_comm->visualize_obstacles(ros::this_node::getName().substr(1,ros::this_node::getName().length())+"/"+ simulator->get_pathname());
            }

            init_collision_list(reader->get_child("application").get());

            PRX_DEBUG_COLOR(" collision_list has been initialized ", PRX_TEXT_GREEN);

            hash_t<std::string, int> comm_systems;

            std::vector<plant_t*> get_plants;
                //SGC: only need plant pointers here
            sys_graph.get_plants(get_plants);

            foreach(plant_t* sys, get_plants)
            {
                    // PRX_DEBUG_COLOR("Plant: " << sys->get_pathname(), PRX_TEXT_GREEN);
                if( !sys->is_active() && visualize )
                {
                    vis_comm->visualize_plant(sys->get_pathname(), PRX_VIS_TEMPORARY_REMOVE);
                }
            }

            if (simulator->get_sensing_model() != NULL)
            {
                PRX_WARN_S ("Sensing model not null, proceeding with initialization of sensors.");
                initialize_sensing();
            }
            else
            {
                PRX_ERROR_S ("Sensing model is null, cannot initialize sensors.");
            }

            if( visualize )
            {
                vis_comm->publish_markers();
            }

            visualization_multiple = ceil(.02/simulation::simulation_step);
            visualization_counter = 0;

            object_publisher = n.advertise<prx_simulation::object_msg>("prx/object_state", 100);

            bin_names.push_back("A");
            bin_names.push_back("B");
            bin_names.push_back("C");
            bin_names.push_back("D");
            bin_names.push_back("E");
            bin_names.push_back("F");
            bin_names.push_back("G");
            bin_names.push_back("H");
            bin_names.push_back("I");
            bin_names.push_back("J");
            bin_names.push_back("K");
            bin_names.push_back("L");


            child_state_space = manipulator->get_state_space();
            real_robot = reader->get_attribute_as<bool>("application/real_robot",false);
            no_sensing = reader->get_attribute_as<bool>("application/no_sensing",false);
            if(real_robot)
            {
                real_robot_state = n.subscribe("/joint_states",4,&apc_sim_application_t::real_robot_state_callback,this);
                robot_state = child_state_space->alloc_point();
                
                name_index_map["torso_joint_b1"] = 0;
                name_index_map["torso_joint_b2"] = 0;

                name_index_map["arm_left_joint_1_s"] = 1;
                name_index_map["arm_left_joint_2_l"] = 2;
                name_index_map["arm_left_joint_3_e"] = 3;
                name_index_map["arm_left_joint_4_u"] = 4;
                name_index_map["arm_left_joint_5_r"] = 5;
                name_index_map["arm_left_joint_6_b"] = 6;
                name_index_map["arm_left_joint_7_t"] = 7;
                name_index_map["head_hinge"] = 8;

                name_index_map["arm_right_joint_1_s"] = 9;
                name_index_map["arm_right_joint_2_l"] = 10;
                name_index_map["arm_right_joint_3_e"] = 11;
                name_index_map["arm_right_joint_4_u"] = 12;
                name_index_map["arm_right_joint_5_r"] = 13;
                name_index_map["arm_right_joint_6_b"] = 14;
                name_index_map["arm_right_joint_7_t"] = 15;
                robot_ac = new actionlib::SimpleActionClient<control_msgs::FollowJointTrajectoryAction>("joint_trajectory_action", true);
                
                unigripper_ac = new actionlib::SimpleActionClient<control_msgs::FollowJointTrajectoryAction>("unigripper/unigripper_joint_trajectory_action", true);

                unigripper_command.trajectory.joint_names.push_back("head_hinge");
                for( int i = 0; i < 16; i++ )
                    robot_command.trajectory.joint_names.push_back(dof_names[i]);
                
            }
            controller->real_robot = real_robot;
            output_control_space = controller->get_output_control_space();
            state_publisher = n.advertise<prx_simulation::state_msg>("prx/manipulator_state", 1);               

            if (!real_robot || no_sensing)
            {
                //Object pose
                std::vector<double> dummy = {0.8,0,1.3,0,0,0,1};
                object_pose = reader->get_attribute_as<std::vector<double> >("application/object_pose",dummy);
            }
            
            Init_Finished = true;


            // std::string work_order_json_file_name("/home/psarakisz89/PRACSYS/src/prx_packages/apc/simulation/apc_pick_task.json");
            char* w = std::getenv("PRACSYS_PATH");
            std::string dir(w);
            std::string output_dir(w);

            if (!json_file.empty())
                dir +=("/prx_packages/apc/simulation/"+json_file);
            else
                dir += ("/prx_packages/apc/simulation/apc_pick_task.json");

            output_dir += ("/prx_output/OUTPUT.json");
            output_json_path = output_dir;

            std::string work_order_json_file_name(dir);

            number_of_orders = read_file(work_order_json_file_name, work_order, bin_contents);

            PRX_PRINT("number_of_orders: "<<number_of_orders, PRX_TEXT_GREEN);
        }

        void apc_sim_application_t::unigripper_callback(const std_msgs::Bool& msg)
        {
            grasp_success = msg.data;
                // PRX_PRINT("\n\n\n\n\n\n\n\n\n grasp_success: \n\n\n\n"<<grasp_success, PRX_TEXT_CYAN);
        }

        void apc_sim_application_t::real_robot_state_callback(const sensor_msgs::JointState& stateMsg)
        {
            block();
            child_state_space->copy_to_point(robot_state);
            sensor_msgs::JointState start_state = stateMsg;
            for( unsigned i = 0; i < start_state.name.size(); i++ )
            {
                robot_state->at(name_index_map[start_state.name[i]]) = start_state.position[i];
            }

            if (automaton_state == MOVE_TO_HOME)
            {
                robot_state->at(16) = 1;
                robot_state->at(17) = 1;
                robot_state->at(18) = 1;
                robot_state->at(19) = 1;
            }

            child_state_space->copy_from_point(robot_state);
            publish_state();
            unblock();
                //possibly we need to add one more check here in order to check when the vacuum is turned on FOR THE FIRST TIME and ignore it. 
                //we will have to test it and see how it behaves. Chances are that we need to do this extra check here 
                //logic
                //if the robot state contains the vacuum control for the first time and the state of the automaton is execute grasping ignore it
                //I cannot make the change right now because I cannot print out the robot state and see how it is structured.

            // if (actual_grasp){
            //         // PRX_PRINT("robot callback: simulation_state: "<<simulator->get_state_space()->print_point(simulation_state), PRX_TEXT_GREEN);
            //         // PRX_PRINT("robot_state: "<<child_state_space->print_point(robot_state, 2), PRX_TEXT_RED);

            //     // PRX_PRINT("simulation_state 5: "<<simulator->get_state_space()->print_point(simulation_state), PRX_TEXT_GREEN);
            // }

                // if (actual_grasp && robot_state->at(16) != 2 && automaton_state == EXECUTE_GRASP)
                // {
                //     PRX_PRINT("robot_state: "<<child_state_space->print_point(robot_state, 2), PRX_TEXT_RED);
                //     simulator->get_state_space()->copy_to_point(simulation_state);
                //     PRX_PRINT("original simulation_state: "<<simulator->get_state_space()->print_point(simulation_state), PRX_TEXT_GREEN);
                //     output_control_space->copy_from_point(unigripper_control_on);
                //     simulator->propagate(simulation::simulation_step);
                //     simulator->get_state_space()->copy_to_point(simulation_state);
                //     PRX_PRINT("after propagate control, simulation_state: "<<simulator->get_state_space()->print_point(simulation_state), PRX_TEXT_GREEN);

                //     // sim::state_t* temp_state;                    
                //     // temp_state = simulator->get_state_space()->alloc_point();
                //     int last_index = simulator->get_state_space()->get_dimension();
                //     PRX_PRINT("target_index: "<<target_index, PRX_TEXT_GREEN);
                //     PRX_PRINT("CurrentTarget: "<<CurrentTarget, PRX_TEXT_GREEN);
                //     simulation_state->at(last_index-2)=target_index;


                //     simulator->push_state(simulation_state);
                //     PRX_PRINT("after push temp state, simulation_state: "<<simulator->get_state_space()->print_point(simulation_state), PRX_TEXT_GREEN);

                //     simulator->get_state_space()->copy_to_point(simulation_state);
                //     PRX_PRINT("after push temp state, simulation_state from simulator: "<<simulator->get_state_space()->print_point(simulation_state), PRX_TEXT_GREEN);

                //     simulator->propagate(simulation::simulation_step);
                //     simulator->get_state_space()->copy_to_point(simulation_state);
                //     PRX_PRINT("after second propagate control, simulation_state: "<<simulator->get_state_space()->print_point(simulation_state), PRX_TEXT_GREEN);

                //     robot_state->at(16) = 2;

            // PRX_PRINT("robot_state: "<<child_state_space->print_point(robot_state, 5), PRX_TEXT_RED);
                // }
            // if (automaton_state == EXECUTE_PLACING)
            // {
            //     controller->send_zero_control();
            //         // simulator->get_state_space()->copy_to_point(simulation_state);
            //         // PRX_PRINT("simulation_state 4: "<<simulator->get_state_space()->print_point(simulation_state), PRX_TEXT_GREEN);

            //     simulator->propagate(simulation::simulation_step);
            //         // simulator->get_state_space()->copy_to_point(simulation_state);
            //         // PRX_PRINT("simulation_state 5: "<<simulator->get_state_space()->print_point(simulation_state), PRX_TEXT_GREEN);
            //         // update_simulation();
            //         // control_t* zero_control = simulation_control_space->alloc_point();
            //         // simulator->push_control(zero_control);
            //         // simulator->propagate_and_respond();
            //         // simulation_state_space->copy_to_point(simulation_state);
            // }



        }


        void apc_sim_application_t::publish_state()
        {
            prx_simulation::state_msg msg;
            for(unsigned i=0;i<child_state_space->get_dimension();++i)
            {
                msg.elements.push_back(child_state_space->at(i));
            }
            state_publisher.publish(msg);
        }

        void apc_sim_application_t::frame(const ros::TimerEvent& event)
        {
            handle_key();

            if( simulator_mode == 1 )
            {
                if( simulator_counter > 0 )
                {
                    simulator_running = true;
                    simulator_counter--;
                    loop_timer.reset();
                }
                else
                    simulator_running = false;
            }
            if( loop_timer.measure() > 1.0 )
                loop_timer.reset();
            if( simulator_running & Init_Finished)
            {
                    // PRX_PRINT(simulator->get_state_space()->print_memory(),PRX_TEXT_BROWN);
                    // PRX_PRINT(simulator->get_state_space()->print_point(simulation_state),PRX_TEXT_CYAN);
                    // simulator->push_state(simulation_state);
                    // PRX_PRINT(simulator->get_state_space()->print_memory(),PRX_TEXT_BROWN);
                    // PRX_PRINT("-----",PRX_TEXT_RED);
                prx_planning::apc_queryGoal command;
                prx_planning::apc_queryResult result;
                prx_simulation::send_trajectoryGoal execute_command;


                if(executing_trajectory == true && !real_robot)
                {
                    if(execution_time>plan_duration*0.2 && automaton_state == EVALUATE_GRASP && start_servoing())
                    {
                        controller->cancel_goal();
                            //LOGIC
                        automaton_state = GRASP_PLANNING;
                        plan_duration = 0.0;
                        execution_time = 0.0;
                        executing_trajectory = false;
                        update_simulation();
                        PRX_PRINT("target_obj_pose changed, will replan",PRX_TEXT_BROWN);
                    }
                    if(execution_time<plan_duration)
                    {
                        // if( ((int)(execution_time*100))%100 == 0)
                        PRX_STATUS("Executing Trajectory"<<execution_time <<"/"<<plan_duration,PRX_TEXT_CYAN);
                        execution_time+=2*simulation::simulation_step;
                            // update_object_poses(command.object);
                        update_simulation();
                    }
                    else
                    {
                        update_simulation();
                        plan_duration = 0.0;
                        execution_time = 0.0;
                        executing_trajectory = false;
                        PRX_PRINT("Trajectory executed",PRX_TEXT_BROWN);
                    }
                }
                else if( automaton_state == START)
                {
                    reset_object_pose();
                    PRX_PRINT("Current State: "<<getStringFromEnum(automaton_state),PRX_TEXT_CYAN);

                    ros::NodeHandle unigripper_success;
                    grasp_success_sub = unigripper_success.subscribe("/unigripper_grasp_success",10,&apc_sim_application_t::unigripper_callback, this);


                        // for (int i = 0; i < 10; ++i)
                        // {   
                        // //     std::string temp = "bin_";
                        // //     std::string bin_name = work_order.bin[i].substr(work_order.bin[i].length() - 1, 1);
                        // //     temp += bin_name;
                        //     // PRX_PRINT("current_item_index: "<<i, PRX_TEXT_GREEN);
                        //     if (bin_contents.bin_A[i].size()>0)
                        //     {
                        //         PRX_PRINT("current bin_contents: "<<bin_contents.bin_A[i], PRX_TEXT_GREEN);
                        //     }

                        // }

                    if (real_robot)
                    {
                        PRX_PRINT("robot_state: "<<child_state_space->print_point(robot_state, 2), PRX_TEXT_RED);
                        std::vector<double> current_position, position_diff;
                        child_state_space->copy_point_to_vector(robot_state, current_position);
                        std::transform(current_position.begin(), current_position.end(), home_position.begin(),
                        std::back_inserter(position_diff), [&](double l, double r)
                        {
                        return std::abs(l - r);
                        });

                        double position_diff_norm = 0;
                        position_diff_norm = l2_norm(position_diff);
                        PRX_PRINT("position_diff_norm: "<< position_diff_norm, PRX_TEXT_CYAN)
                        if (position_diff_norm <= 0.06)
                        {
                            currently_in_home_position = true;
                            PRX_INFO_S("currently_in_home_position: "<<currently_in_home_position);
                        }
                        else{
                            currently_in_home_position = false;
                        }
                    }
                    



                    if(!currently_in_home_position)
                    {
                        automaton_state = MOVE_TO_HOME;
                    }
                    else if(!shelf_detection_done)
                    {
                        PRX_PRINT("shelf_detection_done: "<<shelf_detection_done, PRX_TEXT_CYAN);
                        automaton_state = SHELF_DETECTION;
                    }
                    else
                    {
                        automaton_state = TARGET_SELECTION;
                    }
                } 
                else if( automaton_state == SHELF_DETECTION)
                {
                    PRX_PRINT("Current State: "<<getStringFromEnum(automaton_state),PRX_TEXT_CYAN);
                        // estimate_shelf_position();
                    if(!real_robot || no_sensing)
                    {
                        config_t new_config;
                        foreach(sim::obstacle_t* obst, obstacles)
                        {
                            // PRX_PRINT(obstacles.get_pathname(),PRX_TEXT_RED);
                            PRX_PRINT("SIM APP: Obstacle = "<<obst->get_pathname()<<", config: " <<obst->get_root_configuration().print(),PRX_TEXT_GREEN);
                            if(obst->get_pathname().find("shelf")!=std::string::npos) 
                            {
                                shelf = obst;
                                PRX_PRINT("Found the Shelf"<<shelf->get_root_configuration().print(),PRX_TEXT_RED);
                                double x,y,z,qx,qy,qz,qw;
                                x = 1.24;
                                y = 0;
                                z = 1.11;
                                qx = 0;
                                qy = 0;
                                qz = 0;
                                qw = 1;
                                new_config.set_position(x,y,z);
                                new_config.set_orientation(qx,qy,qz,qw);
                                shelf->update_root_configuration(new_config);
                                shelf->update_collision_info();

                                //send shelf pose to planning
                                objectMsg.name = "shelf";
                                std::vector<double> shelf_pose_vec;
                                new_config.get_position(shelf_pose_vec);
                                shelf_pose_vec.push_back(qx);
                                shelf_pose_vec.push_back(qy);
                                shelf_pose_vec.push_back(qz);
                                shelf_pose_vec.push_back(qw);
                                objectMsg.elements = shelf_pose_vec;
                                object_publisher.publish(objectMsg);
                                PRX_PRINT("Updated the shelf's position: "<<shelf->get_root_configuration().print(),PRX_TEXT_RED);
                                shelf = NULL;
                            }

                        }
                        //sensing srv prx/sensing/update_shelf_pose
                        automaton_state = TARGET_SELECTION;
                        // automaton_state = PLAN_BIN_TRAJECTORIES;
                        // automaton_state = SHELF_DETECTION;                        
                    }
                    else
                    {
                        estimate_shelf_position();
                        sleep(1);
                            //sensing srv prx/sensing/update_shelf_pose
                        automaton_state = TARGET_SELECTION;
                            // automaton_state = PLAN_BIN_TRAJECTORIES;
                            // automaton_state = SHELF_DETECTION;
                    }
                    shelf_detection_done = true;
                    PRX_PRINT("shelf_detection_done: "<<shelf_detection_done, PRX_TEXT_CYAN);
                }
                else if( automaton_state == TARGET_SELECTION)
                {
                    nr_execution_failures = 0;
                    nr_grasping_failures = 0;

                    PRX_PRINT("Current State: "<<getStringFromEnum(automaton_state),PRX_TEXT_CYAN);
                        //logic
                        //else if no more target, automaton_state = END

                    bool found = false;
                    int counter = 0;
                    while (!found && counter < object_priority_list.size())
                    {
                        PRX_PRINT("current looking for "<<" item "<<counter <<": "<< object_priority_list[counter], PRX_TEXT_GREEN);
                        for (int j = 0; j < number_of_orders+1; ++j)
                        {
                            //PRX_PRINT("in the work order: "<< work_order.item[j], PRX_TEXT_GREEN);
                            if (object_priority_list[counter] ==  work_order.item[j]&& !work_order.done[j])
                            {
                                auto it = object_priority_list.begin() + counter;
                                std::rotate(it, it + 1, object_priority_list.end());   
                                current_item_index = j;  
                                PRX_PRINT("Found : "<< object_priority_list[counter], PRX_TEXT_RED);
                                // std::rotate(it, it + 1, object_priority_list.end());   
                                // current_item_index = j;  

                                found = true;
                                std::string bin_name = work_order.bin[j].substr(work_order.bin[j].length() - 1, 1);

                                CurrentBin = bin_name;
                                // CurrentArm = "right";
                                //CurrentArm = arms[i];
                                CurrentTarget = work_order.item[j].c_str();
                                // This determines, based on the object's first entry in the e.e. prioritization list, which arm to assign for move_and_detect, etc.
                                CurrentArm = ((object_to_prioritized_end_effector_context[CurrentTarget].front().find("left") != std::string::npos ) ? "left" : "right");
                                // current_item_index = i;                                
                                PRX_PRINT("CurrentBin: "<<CurrentBin, PRX_TEXT_GREEN);
                                PRX_PRINT("CurrentTarget: "<<CurrentTarget, PRX_TEXT_GREEN);
                                PRX_PRINT("CurrentArm: "<<CurrentArm, PRX_TEXT_GREEN);
                                automaton_state = MOVE_AND_SENSE;

                                estimate_objects_pose();

                                break;
                            }
                        }
                        counter++;
                    }



                    // for (int i = current_item_index; i < number_of_orders + 1; ++i)
                    // {   
                    //     PRX_PRINT("current_item_index: "<<i, PRX_TEXT_GREEN);
                    //     PRX_PRINT("current order: "<<work_order.done[i], PRX_TEXT_GREEN);
                    //     if (work_order.done[i] == false)
                    //     {   
                    //         std::string bin_name = work_order.bin[i].substr(work_order.bin[i].length() - 1, 1);

                    //         CurrentBin = bin_name;
                    //         // CurrentArm = "right";
                    //         //CurrentArm = arms[i];
                    //         CurrentTarget = work_order.item[i].c_str();
                    //         // This determines, based on the object's first entry in the e.e. prioritization list, which arm to assign for move_and_detect, etc.
                    //         CurrentArm = ((object_to_prioritized_end_effector_context[CurrentTarget].front().find("left") != std::string::npos ) ? "left" : "right");
                    //         current_item_index = i;                                
                    //         PRX_PRINT("CurrentBin: "<<CurrentBin, PRX_TEXT_GREEN);
                    //         PRX_PRINT("CurrentTarget: "<<CurrentTarget, PRX_TEXT_GREEN);
                    //         PRX_PRINT("CurrentArm: "<<Curren÷tArm, PRX_TEXT_GREEN);
                    //         automaton_state = MOVE_AND_SENSE;

                    //             // for testing only, force it to try next item
                    //         work_order.done[current_item_index] = true;

                    //         estimate_objects_pose();

                    //         break;

                    //             // PRX_PRINT("bin_name: "<<bin_name, PRX_TEXT_GREEN);
                    //             // if (bin_name=="A"){
                    //             //     CurrentBin = bin_name;
                    //             //     CurrentArm = "left";
                    //             //     CurrentTarget = work_order.item[i].c_str();
                    //             //     current_item_index = i;                                
                    //             //     PRX_PRINT("CurrentTarget: "<<CurrentTarget, PRX_TEXT_GREEN);
                    //             //     break;
                    //             // }
                    //     }
                    // }
                    if (automaton_state != MOVE_AND_SENSE)
                    {
                        automaton_state = END;
                    }

                        //ZP
                        // automaton_state = MOVE_AND_SENSE;
                }
                else if( automaton_state == MOVE_AND_SENSE)
                {                                
                    PRX_PRINT("--------------MOVE_AND_SENSE-----------------", PRX_TEXT_LIGHTGRAY);
                    PRX_PRINT("CurrentBin: "<<CurrentBin, PRX_TEXT_GREEN);
                    PRX_PRINT("CurrentTarget: "<<CurrentTarget, PRX_TEXT_GREEN);
                    PRX_PRINT("CurrentArm: "<<CurrentArm, PRX_TEXT_GREEN);
                    PRX_PRINT("--------------MOVE_AND_SENSE-----------------", PRX_TEXT_LIGHTGRAY);
                    PRX_PRINT("Current State: "<<getStringFromEnum(automaton_state),PRX_TEXT_CYAN);
                        //play saved traj   
                        //5 means EXECUTE_SAVED_TRAJ (refer to apc_query.action)
                    // for (int i = 0; i < 2; ++i)
                    // {
                    std::stringstream path_name;
                    path_name << "three_stage_traj" << "_" << CurrentArm << "/" << CurrentArm << "_" << CurrentBin << "_" << 0;
                    PRX_PRINT(path_name.str(),PRX_TEXT_BROWN);

                    command.stage = 5;
                    command.saved_plan_path = path_name.str();                      

                    PRX_PRINT(command.saved_plan_path,PRX_TEXT_BROWN);
                    planning_app_query->sendGoal(command);
                    bool planning_success = false;
                    planning_success = planning_app_query->waitForResult(ros::Duration(0));
                    result = *planning_app_query->getResult();
                    if(planning_success && result.plan.size()>0)
                    {
                        PRX_PRINT("Received Plan Successfully...",PRX_TEXT_CYAN);
                        execute_command.plan=result.plan;                        

                        controller->convert_and_copy_to_robot_plan(execute_command.plan);
                        controller->set_robot_plan();

                        if(real_robot)
                        {
                            create_and_send_robot_command();
                        }
                        plan_duration = result.duration;
                        executing_trajectory = true;
                        PRX_PRINT("Executing Plan..."<<plan_duration,PRX_TEXT_CYAN);
                        currently_in_home_position = false;
                        clear_plan();
                        automaton_state = MOVE_AND_SENSE_TWO;
                    }
                        // if (i==0){sleep(5);}
                        // else if (i==1){sleep(3);}
                        // else if (i==2){sleep(2);}                                      
                    // }
                    // automaton_state = POSE_ESTIMATION;
                    // std::stringstream path_name;
                    // path_name << CurrentArm << "_" << CurrentBin;
                    // PRX_PRINT(path_name.str(),PRX_TEXT_BROWN);

                    // command.stage = 5;
                    // command.saved_plan_path = path_name.str();

                    // // command.stage = 1;
                    // // command.hand = CurrentArm;
                    // // command.bin = CurrentBin;
                    

                    // PRX_PRINT(command.saved_plan_path,PRX_TEXT_BROWN);
                    // planning_app_query->sendGoal(command);
                    // bool planning_success = false;
                    // planning_success = planning_app_query->waitForResult(ros::Duration(0));
                    // result = *planning_app_query->getResult();
                    // if(planning_success && result.plan.size()>0)
                    // {
                    //     PRX_PRINT("Received Plan Successfully...",PRX_TEXT_CYAN);
                    //     execute_command.plan=result.plan;                        

                    //     controller->convert_and_copy_to_robot_plan(execute_command.plan);
                    //     controller->set_robot_plan();

                    //         //ZPP
                    //     if(real_robot)
                    //     {
                    //         create_and_send_robot_command();
                    //     }
                    //     plan_duration = result.duration;
                    //     executing_trajectory = true;
                    //     PRX_PRINT("Executing Plan...",PRX_TEXT_CYAN);
                    //     currently_in_home_position = false;
                    //     clear_plan();
                            
                    //     automaton_state = POSE_ESTIMATION;
                    // }
                }
                else if( automaton_state == MOVE_AND_SENSE_TWO)
                {
                    PRX_PRINT("--------------MOVE_AND_SENSE_TWO-----------------", PRX_TEXT_LIGHTGRAY);
                    PRX_PRINT("CurrentBin: "<<CurrentBin, PRX_TEXT_GREEN);
                    PRX_PRINT("CurrentTarget: "<<CurrentTarget, PRX_TEXT_GREEN);
                    PRX_PRINT("CurrentArm: "<<CurrentArm, PRX_TEXT_GREEN);
                    PRX_PRINT("--------------MOVE_AND_SENSE_TWO-----------------", PRX_TEXT_LIGHTGRAY);
                    PRX_PRINT("Current State: "<<getStringFromEnum(automaton_state),PRX_TEXT_CYAN);
                    // sleep(3);
                        //play saved traj   
                        //5 means EXECUTE_SAVED_TRAJ (refer to apc_query.action)
                    // for (int i = 0; i < 2; ++i)
                    // {


                    if (move_to_next_bin)
                    {
                        std::stringstream path_name;
                        path_name << "bin_to_bin_traj" << "_" << CurrentArm << "/" << CurrentArm << "_" << lastBin << "_to_" << CurrentBin;
                        PRX_PRINT(path_name.str(),PRX_TEXT_BROWN);

                        command.stage = 5;
                        command.saved_plan_path = path_name.str();                      

                        PRX_PRINT(command.saved_plan_path,PRX_TEXT_BROWN);
                        move_to_next_bin = false;
                    }
                    else
                    {
                        std::stringstream path_name;
                        path_name << "three_stage_traj" << "_" << CurrentArm << "/" << CurrentArm << "_" << CurrentBin << "_" << 1;
                        PRX_PRINT(path_name.str(),PRX_TEXT_BROWN);

                        command.stage = 5;
                        command.saved_plan_path = path_name.str();                      

                        PRX_PRINT(command.saved_plan_path,PRX_TEXT_BROWN);
                    }



                    planning_app_query->sendGoal(command);
                    bool planning_success = false;
                    planning_success = planning_app_query->waitForResult(ros::Duration(0));
                    result = *planning_app_query->getResult();
                    if(planning_success && result.plan.size()>0)
                    {
                        PRX_PRINT("Received Plan Successfully...",PRX_TEXT_CYAN);
                        execute_command.plan=result.plan;                        

                        controller->convert_and_copy_to_robot_plan(execute_command.plan);
                        controller->set_robot_plan();

                        if(real_robot)
                        {
                            create_and_send_robot_command();
                        }
                        plan_duration = result.duration;
                        executing_trajectory = true;
                        PRX_PRINT("Executing Plan..."<<plan_duration,PRX_TEXT_CYAN);
                        currently_in_home_position = false;
                        clear_plan();
                        automaton_state = MOVE_AND_SENSE_THREE;
                    }
                }
                else if( automaton_state == MOVE_AND_SENSE_THREE)
                {
                    PRX_PRINT("Current State: "<<getStringFromEnum(automaton_state),PRX_TEXT_CYAN);
                    sleep(3);
                        //play saved traj   
                        //5 means EXECUTE_SAVED_TRAJ (refer to apc_query.action)
                    // for (int i = 0; i < 2; ++i)
                    // {
                    std::stringstream path_name;
                    path_name << "three_stage_traj" << "_" << CurrentArm << "/" << CurrentArm << "_" << CurrentBin << "_" << 2;
                    PRX_PRINT(path_name.str(),PRX_TEXT_BROWN);

                    command.stage = 5;
                    command.saved_plan_path = path_name.str();                      

                    PRX_PRINT(command.saved_plan_path,PRX_TEXT_BROWN);
                    planning_app_query->sendGoal(command);
                    bool planning_success = false;
                    planning_success = planning_app_query->waitForResult(ros::Duration(0));
                    result = *planning_app_query->getResult();
                    if(planning_success && result.plan.size()>0)
                    {
                        PRX_PRINT("Received Plan Successfully...",PRX_TEXT_CYAN);
                        execute_command.plan=result.plan;                        

                        controller->convert_and_copy_to_robot_plan(execute_command.plan);
                        controller->set_robot_plan();

                        if(real_robot)
                        {
                            create_and_send_robot_command();
                        }
                        plan_duration = result.duration;
                        executing_trajectory = true;
                        PRX_PRINT("Executing Plan..."<<plan_duration,PRX_TEXT_CYAN);
                        currently_in_home_position = false;
                        clear_plan();

                        automaton_state = POSE_ESTIMATION;

                        // use to only run mapping traj
                        // automaton_state = MOVE_TO_HOME;

                        // sleep(3);
                    }
                }
                else if( automaton_state == POSE_ESTIMATION)
                {

                    PRX_PRINT("--------------POSE_ESTIMATION-----------------", PRX_TEXT_LIGHTGRAY);
                    PRX_PRINT("CurrentBin: "<<CurrentBin, PRX_TEXT_GREEN);
                    PRX_PRINT("CurrentTarget: "<<CurrentTarget, PRX_TEXT_GREEN);
                    PRX_PRINT("CurrentArm: "<<CurrentArm, PRX_TEXT_GREEN);
                    PRX_PRINT("--------------POSE_ESTIMATION-----------------", PRX_TEXT_LIGHTGRAY);
                    PRX_PRINT("Current State: "<<getStringFromEnum(automaton_state),PRX_TEXT_CYAN);
                    // based on sensing srv(prx/sensing/publish_object_list) to set rearrangement_flag
                    
                    z_axis_offset = 0.0;

                    if (!real_robot || no_sensing)
                    {
                        foreach(movable_body_plant_t* plant, movable_bodies)
                        {
                            if(plant->get_object_type() == CurrentTarget)
                            {
                                PRX_PRINT("CurrentTarget: "<<CurrentTarget,PRX_TEXT_CYAN);

                                target_index = counter;
                                PRX_PRINT("target_index: "<<target_index, PRX_TEXT_GREEN);

                                std::vector<double> obj_pose_vector;
                                config_t obj_pose;
                                //hardcode here some position

                                obj_pose.set_position(object_poses[CurrentTarget].at(0),object_poses[CurrentTarget].at(1),object_poses[CurrentTarget].at(2));
                                obj_pose.set_orientation(object_poses[CurrentTarget].at(3),object_poses[CurrentTarget].at(4),object_poses[CurrentTarget].at(5), object_poses[CurrentTarget].at(6));

                                plant->set_configuration(obj_pose);
                                obj_pose.copy_to_vector(target_obj_pose);
                                PRX_PRINT("target_obj_pose: " << target_obj_pose, PRX_TEXT_GREEN);
                                plant->update_collision_info();

                                if(!check_object_collision(plant, 0))
                                {
                                    // move to next bin
                                    // move_to_next_bin = true;
                                    // lastBin = CurrentBin;
                                    // reset_object_pose();
                                    // automaton_state = TARGET_SELECTION;
                                    
                                    // move to home
                                    automaton_state = MOVE_TO_HOME; 
                                }
                                else
                                {
                                    objectMsg.name = CurrentTarget;
                                    // std::vector<double> pose_vec;
                                    // double quat[4];
                                    // obj_pose.get_position(pose_vec);
                                    // target_obj_pose.get_xyzw_orientation(quat);
                                    // pose_vec.push_back(quat[0]);
                                    // pose_vec.push_back(quat[1]);
                                    // pose_vec.push_back(quat[2]);
                                    // pose_vec.push_back(quat[3]);
                                    objectMsg.elements = target_obj_pose;
                                    object_publisher.publish(objectMsg);
                                    plant = NULL;

                                    //test rearrangement
                                    // rearrangement_flag = true;
                                    if (rearrangement_flag == true)
                                    {
                                        automaton_state = PLAN_FOR_BLOCKING_ITEM; 
                                    }
                                    else
                                    {
                                        automaton_state = GRASP_PLANNING; 
                                    }  
                                }
                                                           
                            }
                        }
                    }
                    else
                    {
                        bool sensing_success;
                        //estimate_objects_pose();
                        sensing_success = update_objects_pose();

                        //test rearrangement
                        // rearrangement_flag = true;

                        if (sensing_success)
                        {
                            
                            foreach(movable_body_plant_t* plant, movable_bodies)
                            {

                                if(plant->get_object_type() == CurrentTarget)
                                {
                                    target_index = counter;
                                    PRX_PRINT("target_index: "<<target_index, PRX_TEXT_GREEN);

                                    std::vector<double> obj_pose_vector;
                                    config_t obj_pose;
                                    plant->get_configuration(obj_pose);
                                    obj_pose.copy_to_vector(obj_pose_vector);
                                    obj_pose.copy_to_vector(target_obj_pose);
                                    if (rearrangement_flag == true)
                                    {
                                        PRX_INFO_S("Rearrangement");
                                        automaton_state = BLOCKING_ITEM_SELECTION;
                                    }
                                    else{
                                        PRX_INFO_S("GRASP_PLANNING");
                                        automaton_state = GRASP_PLANNING; 
                                    }
                                        // if (obj_pose_vector[1] < .5)
                                        // {
                                        //     // automaton_state = GRASP_PLANNING; 
                                        //     automaton_state = GRASP_PLANNING; 
                                        // }                                
                                }
                            }
                            sensing_counter = 0; 
                        }
                        else if (sensing_counter < 3)
                        {
                            PRX_PRINT("sensing falied, counter: "<<sensing_counter, PRX_TEXT_RED);
                            sensing_counter++;
                            automaton_state = POSE_ESTIMATION;
                        }
                        else
                        {
                            PRX_INFO_S("Sensing Falied, will try next item");
                            sensing_counter = 0; 
                            // current_item_index++;
                            
                            // move_to_next_bin = true;
                            // lastBin = CurrentBin;
                            // reset_object_pose();
                            // automaton_state = TARGET_SELECTION;

                            // move to home
                            automaton_state = MOVE_TO_HOME;
                        }
                    }


                        //test, Shaojun
                        // std::vector<manipulation::movable_body_plant_t*> objects;
                        // simulator->get_movable_objects(objects);

                        // double counter2 = 0;
                        // foreach(movable_body_plant_t* plant, objects)
                        // {

                        //     if(plant->get_object_type() == CurrentTarget)
                        //     {
                        //         target_index2 = counter2;
                        //         PRX_PRINT("target_index2: "<<target_index2, PRX_TEXT_GREEN);                             
                        //     }
                        //     counter2++;
                        // }                            

                }
                else if( automaton_state == GRASP_PLANNING)
                {
                    PRX_PRINT("---------- GRASP PLANNING ---------------------", PRX_TEXT_LIGHTGRAY);
                    PRX_PRINT("CurrentBin: "<<CurrentBin, PRX_TEXT_GREEN);
                    PRX_PRINT("CurrentTarget: "<<CurrentTarget, PRX_TEXT_GREEN);
                    PRX_PRINT("CurrentArm: "<<CurrentArm, PRX_TEXT_GREEN);
                    PRX_PRINT("---------- GRASP PLANNING ---------------------",PRX_TEXT_LIGHTGRAY);
                    PRX_PRINT("Current State: "<<getStringFromEnum(automaton_state),PRX_TEXT_CYAN);

                        //2 means PERFORM_GRASP (refer to apc_query.action)

                    PRX_PRINT("Beginning of GRASP PLANNING: simulation_state: "<<simulation_state_space->print_point(simulation_state), PRX_TEXT_RED);
                    command.stage = 2;
                    command.object = CurrentTarget;
                    PRX_PRINT("CurrentArm: "<<CurrentArm, PRX_TEXT_GREEN);
                    command.hand = CurrentArm;
                    PRX_PRINT("command.hand: "<<command.hand, PRX_TEXT_GREEN);
                    command.bin = CurrentBin;
                    foreach(movable_body_plant_t* plant, movable_bodies)
                    {
                        if(plant->get_object_type() == command.object)
                        {
                            std::vector<double> obj_pose_vector;
                            config_t obj_pose;
                            plant->get_configuration(obj_pose);
                            obj_pose.copy_to_vector(obj_pose_vector);
                            command.object_state = obj_pose_vector;
                        }
                    }

                    PRX_PRINT(command.object_state,PRX_TEXT_BROWN);

                    planning_app_query->waitForServer();
                    planning_app_query->sendGoal(command);
                    bool planning_success = false;
                    planning_app_query->waitForResult(ros::Duration(0));
                    result = *planning_app_query->getResult();
                    actionlib::SimpleClientGoalState state = planning_app_query->getState();
                    PRX_PRINT("Planning: "<<state.toString(),PRX_TEXT_RED);
                        // PRX_PRINT("Executing Plan..."<<result.duration,PRX_TEXT_CYAN);


                        // if(state.toString()=="SUCCEEDED" && result.plan.size()>0)
                        // {
                        //     PRX_PRINT("Received Plan Successfully...",PRX_TEXT_CYAN);
                        //     // execute_command.plan=result.plan;
                        //     execute_command.plan=result.reaching;
                        //     controller_query->sendGoal(execute_command);
                        //     bool consumer_success = false;
                        //     consumer_success = controller_query->waitForResult(ros::Duration(0));
                        //     plan_duration = result.duration;
                        //     executing_trajectory = true;
                        //     PRX_PRINT("Executing Plan...",PRX_TEXT_CYAN);
                        //     currently_in_home_position = false;

                        // }


                    if (state.toString()=="SUCCEEDED")
                    {
                        current_result = result;

                        //execute the whole plan until we have resensing capability
                        // automaton_state = EXECUTE_PLACING;
                        automaton_state = EXECUTE_REACHING;
                    }
                    else if (rearrangement_flag == true)
                    {
                        PRX_PRINT("Engaging Rearrangement Mode...",PRX_TEXT_CYAN);
                        automaton_state = BLOCKING_ITEM_SELECTION;
                    }
                    else if (state.toString()=="ABORTED")
                    {
                        PRX_PRINT("Grasp Planning Failed...",PRX_TEXT_CYAN);

                        // move from bin to bin
                        // move_to_next_bin = true;
                        // lastBin = CurrentBin;
                        // reset_object_pose();
                        // automaton_state = TARGET_SELECTION;

                        // move to home
                        automaton_state = STOP_ROBOT;
                    }
                        // planning srv; (may set rearrangement_flag = true else if palanning failed)
                }
                else if( automaton_state == BLOCKING_ITEM_SELECTION)
                {
                    PRX_PRINT("Current State: "<<getStringFromEnum(automaton_state),PRX_TEXT_CYAN);
                    automaton_state = PLAN_FOR_BLOCKING_ITEM;
                        //logic
                }
                else if( automaton_state == EXECUTE_REACHING)
                {
                    PRX_PRINT("---------- EXECUTE_REACHING ---------------------", PRX_TEXT_LIGHTGRAY);
                    PRX_PRINT("CurrentBin: "<<CurrentBin, PRX_TEXT_GREEN);
                    PRX_PRINT("CurrentTarget: "<<CurrentTarget, PRX_TEXT_GREEN);
                    PRX_PRINT("CurrentArm: "<<CurrentArm, PRX_TEXT_GREEN);
                    PRX_PRINT("---------- EXECUTE_REACHING ---------------------",PRX_TEXT_LIGHTGRAY);
                    PRX_PRINT("Current State: "<<getStringFromEnum(automaton_state),PRX_TEXT_CYAN);
                        // subscribe topic: prx/sensing/publish_object_list, potetially RGB cam as well

                    bool replan = true;

                        // has_goal=true;
                    execute_command.plan=current_result.reaching;
                    controller->convert_and_copy_to_robot_plan(execute_command.plan);
                    controller->set_robot_plan();
                        //need to create variables to save the robot messages
                    if(real_robot)
                    {
                        create_and_send_robot_command();
                    }



                    plan_duration = current_result.reaching_duration;

                    executing_trajectory = true;
                    PRX_PRINT("Executing Plan..."<<plan_duration,PRX_TEXT_CYAN);
                    currently_in_home_position = false;
                    clear_plan();

                        // pose change evaluation
                        // bool sensing_success;
                        // sensing_success = estimate_objects_pose();
                        // if (sensing_success == true)
                        // {
                        //     foreach(movable_body_plant_t* plant, movable_bodies)
                        //     {
                        //         if(plant->get_object_type() == CurrentTarget)
                        //         {
                        //             std::vector<double> pose_diff_vector;
                        //             config_t obj_pose, pose_diff;
                        //             plant->get_configuration(obj_pose);
                        //             pose_diff = obj_pose - target_obj_pose;
                        //             pose_diff.copy_to_vector(pose_diff_vector);
                        //             double pose_diff_norm;
                        //             pose_diff_norm = l2_norm(pose_diff_vector);
                        //             if (pose_diff_norm > POSE_CAHNGE_THRESHOLD)
                        //             {
                        //                 pose_change = true;
                        //             }
                        //         }
                        //     }
                        // }


                    if (pose_change)
                    {
                        if (replan && nr_execution_failures < 3)
                        {
                            nr_execution_failures++;
                            automaton_state = GRASP_PLANNING;
                        }
                        else{
                            automaton_state = STOP_ROBOT;
                        }
                        pose_change = false;
                    }
                    else{
                        automaton_state = EXECUTE_GRASP;
                    }
                }
                else if( automaton_state == EVALUATE_GRASP)
                {
                    PRX_PRINT("---------- EVALUATE_GRASP ---------------------", PRX_TEXT_LIGHTGRAY);
                    PRX_PRINT("CurrentBin: "<<CurrentBin, PRX_TEXT_GREEN);
                    PRX_PRINT("CurrentTarget: "<<CurrentTarget, PRX_TEXT_GREEN);
                    PRX_PRINT("CurrentArm: "<<CurrentArm, PRX_TEXT_GREEN);
                    PRX_PRINT("---------- EVALUATE_GRASP ---------------------",PRX_TEXT_LIGHTGRAY);
                    PRX_PRINT("Current State: "<<getStringFromEnum(automaton_state),PRX_TEXT_CYAN);

                    PRX_PRINT("grasp_success: "<<grasp_success, PRX_TEXT_GREEN);

                    // fake unigripper sensor gives failure info
                    // grasp_success = false;

                    PRX_PRINT("grasp_success: "<<grasp_success, PRX_TEXT_GREEN);
                    if (grasp_success)
                    {
                        if (nr_grasping_failures > 0)
                        {
                            automaton_state = LIFT; 
                        }
                        else
                        {
                            automaton_state = EXECUTE_PLACING;
                        }       
                    }
                    else if(nr_grasping_failures < 2)
                    {
                        nr_grasping_failures++;
                        automaton_state = RETRY_GRASP;
                    }
                    else
                    {
                        automaton_state = LIFT;                        
                    }
                    // }
                    // else
                    // {
                    //     PRX_PRINT("grasp_success: "<<grasp_success, PRX_TEXT_GREEN);
                    //     if (grasp_success)
                    //     {
                    //         automaton_state = LIFT;
                    //     }
                    //     // else if (good_grasp)
                    //     // {
                    //     //     automaton_state = EXECUTE_PLACING;
                    //     // }
                    //     else if(nr_grasping_failures < 2)
                    //     {
                    //         // automaton_state = PLAN_FOR_TARGET_ITEM;
                    //             // automaton_state = EXECUTE_PLACING;
                    //         nr_grasping_failures++;
                    //         automaton_state = RETRY_GRASP;
                    //     }
                    //     else
                    //     {
                    //         automaton_state = DISENGAGE_EE;
                    //     }
                    // }
                    
                    // if (good_grasp && start_servoing())
                    // {
                    //     automaton_state = ADJUST_EE;
                    // }
                    // else if(good_grasp)
                    // {
                    //     automaton_state = EXECUTE_GRASP;
                    // }
                    // else{
                    //     automaton_state = STOP_ROBOT;
                    // }
                        // Shaojun TODO, sensing from RGB
                }
                else if( automaton_state == ADJUST_EE)
                {
                    PRX_PRINT("Current State: "<<getStringFromEnum(automaton_state),PRX_TEXT_CYAN);
                    bool adjustable = true;
                    if (adjustable)
                    {
                        automaton_state = EXECUTE_GRASP;
                    }
                    else{
                        automaton_state = STOP_ROBOT;
                    }
                        // Shaojun TODO, Jacobian steering

                }
                else if( automaton_state == EXECUTE_GRASP)
                {
                    PRX_PRINT("---------- EXECUTE_GRASP ---------------------", PRX_TEXT_LIGHTGRAY);
                    PRX_PRINT("CurrentBin: "<<CurrentBin, PRX_TEXT_GREEN);
                    PRX_PRINT("CurrentTarget: "<<CurrentTarget, PRX_TEXT_GREEN);
                    PRX_PRINT("CurrentArm: "<<CurrentArm, PRX_TEXT_GREEN);
                    PRX_PRINT("---------- EXECUTE_GRASP ---------------------",PRX_TEXT_LIGHTGRAY);
                    PRX_PRINT("Current State: "<<getStringFromEnum(automaton_state),PRX_TEXT_CYAN);

                    execute_command.plan=current_result.grasping;

                    // for (int i = 0; i < current_result.grasping.size(); ++i)
                    // {
                    //    PRX_PRINT("grasping plan: "<<execute_command.plan[i].control, PRX_TEXT_CYAN);
                    // }
                    PRX_PRINT("grasping plan size: " << current_result.grasping.size(), PRX_TEXT_CYAN);

                    controller->convert_and_copy_to_robot_plan(execute_command.plan);
                    controller->set_robot_plan();

                        //ZPP
                    if(real_robot)
                    {
                            // execute_command.plan[0].control[16] = 2; 
                            // PRX_PRINT("grasping plan: "<<execute_command.plan[0].control, PRX_TEXT_CYAN);
                            // controller->convert_and_copy_to_robot_plan(execute_command.plan);
                            // controller->set_robot_plan();
                        create_and_send_robot_command();
                    }

                    plan_duration = current_result.grasping_duration;

                    executing_trajectory = true;
                    PRX_PRINT("Executing Plan..."<<plan_duration,PRX_TEXT_CYAN);
                    currently_in_home_position = false;
                    clear_plan();


                    PRX_PRINT("grasp_success: "<<grasp_success, PRX_TEXT_GREEN);

                    // fake unigripper sensor gives failure info
                    // grasp_success = false;

                    nr_grasping_failures = 0;

                    automaton_state = EVALUATE_GRASP;

                    // if (!real_robot)
                    // {
                    //     PRX_PRINT("grasp_success: "<<grasp_success, PRX_TEXT_GREEN);
                    //     if (grasp_success)
                    //     {
                    //         automaton_state = EXECUTE_PLACING;
                    //     }
                    //     // else if (good_grasp)
                    //     // {
                    //     //     automaton_state = EXECUTE_PLACING;
                    //     // }
                    //     else if(nr_grasping_failures < 2)
                    //     {
                    //         // automaton_state = PLAN_FOR_TARGET_ITEM;
                    //             // automaton_state = EXECUTE_PLACING;
                    //         nr_grasping_failures++;
                    //         automaton_state = RETRY_GRASP;
                    //     }
                    //     else
                    //     {
                    //         automaton_state = DISENGAGE_EE;
                    //     }
                    // }
                    // else
                    // {
                    //     PRX_PRINT("grasp_success: "<<grasp_success, PRX_TEXT_GREEN);
                    //     if (grasp_success)
                    //     {
                    //         automaton_state = EXECUTE_PLACING;
                    //     }
                    //     // else if (good_grasp)
                    //     // {
                    //     //     automaton_state = EXECUTE_PLACING;
                    //     // }
                    //     else if(nr_grasping_failures < 2)
                    //     {
                    //         // automaton_state = PLAN_FOR_TARGET_ITEM;
                    //             // automaton_state = EXECUTE_PLACING;
                    //         nr_grasping_failures++;
                    //         automaton_state = RETRY_GRASP;
                    //     }
                    //     else
                    //     {
                    //         automaton_state = DISENGAGE_EE;
                    //     }
                    // }


                        // sensing srv, tactile etc.
                }
                else if( automaton_state == RETRY_GRASP)
                {   
                    PRX_PRINT("---------- RETRY_GRASP ---------------------", PRX_TEXT_LIGHTGRAY);
                    PRX_PRINT("CurrentBin: "<<CurrentBin, PRX_TEXT_GREEN);
                    PRX_PRINT("CurrentTarget: "<<CurrentTarget, PRX_TEXT_GREEN);
                    PRX_PRINT("CurrentArm: "<<CurrentArm, PRX_TEXT_GREEN);
                    PRX_PRINT("---------- RETRY_GRASP ---------------------",PRX_TEXT_LIGHTGRAY);
                    PRX_PRINT("Current State: "<<getStringFromEnum(automaton_state),PRX_TEXT_CYAN);
                    command.stage = 13;
                    command.object = CurrentTarget;
                    PRX_PRINT("CurrentArm: "<<CurrentArm, PRX_TEXT_GREEN);
                    command.hand = CurrentArm;
                    PRX_PRINT("command.hand: "<<command.hand, PRX_TEXT_GREEN);
                    command.bin = CurrentBin;
                    foreach(movable_body_plant_t* plant, movable_bodies)
                    {
                        if(plant->get_object_type() == command.object)
                        {
                            std::vector<double> obj_pose_vector;
                            config_t obj_pose;
                            plant->get_configuration(obj_pose);
                            obj_pose.copy_to_vector(obj_pose_vector);
                            command.object_state = obj_pose_vector;
                        }
                    }

                    PRX_PRINT(command.object_state,PRX_TEXT_BROWN);

                    planning_app_query->waitForServer();
                    planning_app_query->sendGoal(command);
                    bool planning_success = false;
                    planning_app_query->waitForResult(ros::Duration(0));
                    regrasp_result = *planning_app_query->getResult();
                    actionlib::SimpleClientGoalState state = planning_app_query->getState();
                    PRX_PRINT("Planning: "<<state.toString(),PRX_TEXT_RED);
                        // PRX_PRINT("Executing Plan..."<<result.duration,PRX_TEXT_CYAN);


                        // if(state.toString()=="SUCCEEDED" && result.plan.size()>0)
                        // {
                        //     PRX_PRINT("Received Plan Successfully...",PRX_TEXT_CYAN);
                        //     // execute_command.plan=result.plan;
                        //     execute_command.plan=result.reaching;
                        //     controller_query->sendGoal(execute_command);
                        //     bool consumer_success = false;
                        //     consumer_success = controller_query->waitForResult(ros::Duration(0));
                        //     plan_duration = result.duration;
                        //     executing_trajectory = true;
                        //     PRX_PRINT("Executing Plan...",PRX_TEXT_CYAN);
                        //     currently_in_home_position = false;

                        // }


                    if (state.toString()=="SUCCEEDED")
                    {
                        // current_result = result;

                        //execute the whole plan until we have resensing capability
                        // automaton_state = EXECUTE_PLACING;
                        automaton_state = EXECUTE_REGRASP;
                    }
                    else if (nr_grasping_failures > 1)
                    {
                        automaton_state = LIFT;
                    }
                    else if (rearrangement_flag == true)
                    {
                        PRX_PRINT("Engaging Rearrangement Mode...",PRX_TEXT_CYAN);
                        automaton_state = BLOCKING_ITEM_SELECTION;
                    }
                    else if (state.toString()=="ABORTED")
                    {
                        PRX_PRINT("Grasp Planning Failed...",PRX_TEXT_CYAN);
                        automaton_state = STOP_ROBOT;
                    }
                }
                else if( automaton_state == EXECUTE_REGRASP)
                {
                    PRX_PRINT("---------- EXECUTE_REGRASP ---------------------", PRX_TEXT_LIGHTGRAY);
                    PRX_PRINT("CurrentBin: "<<CurrentBin, PRX_TEXT_GREEN);
                    PRX_PRINT("CurrentTarget: "<<CurrentTarget, PRX_TEXT_GREEN);
                    PRX_PRINT("CurrentArm: "<<CurrentArm, PRX_TEXT_GREEN);
                    PRX_PRINT("---------- EXECUTE_REGRASP ---------------------",PRX_TEXT_LIGHTGRAY);
                    PRX_PRINT("Current State: "<<getStringFromEnum(automaton_state),PRX_TEXT_CYAN);
                    // send cammand to robot
                    //double original_length = regrasp_result.plan.length();
                    execute_command.plan = regrasp_result.plan;//trim(original_length - 0.02);   
                    execute_command.plan.back().duration -= 0.02;
                    // execute_command.plan=current_result.plan;                        

                    controller->convert_and_copy_to_robot_plan(execute_command.plan);
                    controller->set_robot_plan();
                    
                    //ZPP
                    if(real_robot)
                    {
                        create_and_send_robot_command();
                    }

                    plan_duration = regrasp_result.duration;

                    executing_trajectory = true;
                    PRX_PRINT("Executing Plan..."<<plan_duration,PRX_TEXT_CYAN);
                    currently_in_home_position = false;

                    clear_plan();
                    automaton_state = EVALUATE_GRASP;
                }

                else if( automaton_state == LIFT)
                {   
                    PRX_PRINT("---------- LIFT ---------------------", PRX_TEXT_LIGHTGRAY);
                    PRX_PRINT("CurrentBin: "<<CurrentBin, PRX_TEXT_GREEN);
                    PRX_PRINT("CurrentTarget: "<<CurrentTarget, PRX_TEXT_GREEN);
                    PRX_PRINT("CurrentArm: "<<CurrentArm, PRX_TEXT_GREEN);
                    PRX_PRINT("---------- LIFT ---------------------",PRX_TEXT_LIGHTGRAY);
                    PRX_PRINT("Current State: "<<getStringFromEnum(automaton_state),PRX_TEXT_CYAN);
                    command.stage = 14;
                    command.object = CurrentTarget;
                    PRX_PRINT("CurrentArm: "<<CurrentArm, PRX_TEXT_GREEN);
                    command.hand = CurrentArm;
                    PRX_PRINT("command.hand: "<<command.hand, PRX_TEXT_GREEN);
                    command.bin = CurrentBin;
                    foreach(movable_body_plant_t* plant, movable_bodies)
                    {
                        if(plant->get_object_type() == command.object)
                        {
                            std::vector<double> obj_pose_vector;
                            config_t obj_pose;
                            plant->get_configuration(obj_pose);
                            obj_pose.copy_to_vector(obj_pose_vector);
                            command.object_state = obj_pose_vector;
                        }
                    }

                    //PRX_PRINT(command.object_state,PRX_TEXT_BROWN);

                    planning_app_query->waitForServer();
                    planning_app_query->sendGoal(command);
                    bool planning_success = false;
                    planning_app_query->waitForResult(ros::Duration(0));
                    lift_result = *planning_app_query->getResult();
                    actionlib::SimpleClientGoalState state = planning_app_query->getState();
                    PRX_PRINT("Planning: "<<state.toString(),PRX_TEXT_RED);
                        // PRX_PRINT("Executing Plan..."<<result.duration,PRX_TEXT_CYAN);


                        // if(state.toString()=="SUCCEEDED" && result.plan.size()>0)
                        // {
                        //     PRX_PRINT("Received Plan Successfully...",PRX_TEXT_CYAN);
                        //     // execute_command.plan=result.plan;
                        //     execute_command.plan=result.reaching;
                        //     controller_query->sendGoal(execute_command);
                        //     bool consumer_success = false;
                        //     consumer_success = controller_query->waitForResult(ros::Duration(0));
                        //     plan_duration = result.duration;
                        //     executing_trajectory = true;
                        //     PRX_PRINT("Executing Plan...",PRX_TEXT_CYAN);
                        //     currently_in_home_position = false;

                        // }


                    if (state.toString()=="SUCCEEDED")
                    {
                        // current_result = result;

                        //execute the whole plan until we have resensing capability
                        // automaton_state = EXECUTE_PLACING;
                        automaton_state = EXECUTE_LIFT;
                    }
                    // else if (nr_grasping_failures > 0)
                    // {
                    //     automaton_state = LIFT;
                    // }
                    else if (state.toString()=="ABORTED")
                    {
                        PRX_PRINT("Grasp Planning Failed...",PRX_TEXT_CYAN);
                        automaton_state = STOP_ROBOT;
                    }
                }

                else if( automaton_state == EXECUTE_LIFT)
                {
                    PRX_PRINT("---------- EXECUTE_LIFT ---------------------", PRX_TEXT_LIGHTGRAY);
                    PRX_PRINT("CurrentBin: "<<CurrentBin, PRX_TEXT_GREEN);
                    PRX_PRINT("CurrentTarget: "<<CurrentTarget, PRX_TEXT_GREEN);
                    PRX_PRINT("CurrentArm: "<<CurrentArm, PRX_TEXT_GREEN);
                    PRX_PRINT("---------- EXECUTE_LIFT ---------------------",PRX_TEXT_LIGHTGRAY);

                    PRX_PRINT("Current State: "<<getStringFromEnum(automaton_state),PRX_TEXT_CYAN);
                    // send cammand to robot
                    //double original_length = regrasp_result.plan.length();
                    execute_command.plan = lift_result.plan;//trim(original_length - 0.02);   
                    execute_command.plan.back().duration -= 0.02;
                    // execute_command.plan=current_result.plan;                        

                    controller->convert_and_copy_to_robot_plan(execute_command.plan);
                    controller->set_robot_plan();
                    
                    //ZPP
                    if(real_robot)
                    {
                        create_and_send_robot_command();
                    }

                    plan_duration = lift_result.duration;

                    executing_trajectory = true;
                    PRX_PRINT("Executing Plan..."<<plan_duration,PRX_TEXT_CYAN);
                    currently_in_home_position = false;

                    clear_plan();
                    if (grasp_success)
                    {
                        if (nr_grasping_failures >1)
                        {
                            automaton_state = LIFT;
                            nr_grasping_failures = 0;
                        }
                        else
                        {
                            automaton_state = EXECUTE_PLACING;
                            nr_grasping_failures = 0;                            
                        }
                    }
                    else
                    {
                        if (nr_grasping_failures >1)
                        {
                            automaton_state = LIFT;
                            nr_grasping_failures = 0; 
                        }
                        else
                        {
                            automaton_state = DISENGAGE_EE;
                            nr_grasping_failures = 0; 
                        }
                    }                    
                }
                else if( automaton_state == DISENGAGE_EE)
                {
                    PRX_PRINT("---------- DISENGAGE_EE ---------------------", PRX_TEXT_LIGHTGRAY);
                    PRX_PRINT("CurrentBin: "<<CurrentBin, PRX_TEXT_GREEN);
                    PRX_PRINT("CurrentTarget: "<<CurrentTarget, PRX_TEXT_GREEN);
                    PRX_PRINT("CurrentArm: "<<CurrentArm, PRX_TEXT_GREEN);
                    PRX_PRINT("---------- DISENGAGE_EE ---------------------",PRX_TEXT_LIGHTGRAY);
                    PRX_PRINT("Current State: "<<getStringFromEnum(automaton_state),PRX_TEXT_CYAN);
                        // EE control

                    std::vector<double> control(20, 0.0);

                    // int gripper_control_index;
                    // if (CurrentArm == "left")
                    // {
                    //     PRX_INFO_S("Sending left UniGripper command");                       
                    //     gripper_control_index = 16;
                    // }
                    // else
                    // {
                    //     PRX_INFO_S("Sending right UniGripper command");
                    //     gripper_control_index = 18;
                    // }
                    control[16] = 1;
                    control[17] = 1;
                    control[18] = 1;
                    control[19] = 1;

                    float duration = 0.1;

                    prx_simulation::control_msg control_m;
                    control_m.control = control;
                    control_m.duration = duration;
                    current_result.grasping.clear();
                    current_result.grasping.push_back(control_m);
                    current_result.grasping.back().duration = duration;

                        // PRX_PRINT("control: "<< control, PRX_TEXT_GREEN);
                        // PRX_PRINT("duration: "<< duration, PRX_TEXT_GREEN);
                        // current_result.grasping[0].control = control;
                        // current_result.grasping[0].duration = duration;
                        // PRX_PRINT("current_result.plan.size: "<< current_result.grasping.size(), PRX_TEXT_GREEN);
                        // for (int i = 1; i < current_result.grasping.size(); ++i)
                        // {
                        //     current_result.grasping[i].control = control;
                        //     current_result.grasping[i].duration = 0;
                        // }

                        execute_command.plan = current_result.grasping;//current_result.plan[0];
                        
                        // PRX_PRINT("control: "<< execute_command.plan[0].control, PRX_TEXT_GREEN);
                        // PRX_PRINT("duration: "<< execute_command.plan[0].duration, PRX_TEXT_GREEN);
                        controller->convert_and_copy_to_robot_plan(execute_command.plan);
                        controller->set_robot_plan();
                        
                        //ZPP
                        if(real_robot)
                        {
                            create_and_send_robot_command();
                        }
                        clear_plan();

                        automaton_state = MOVE_TO_HOME;
                        
                        // bool retry_grasp = true;
                        // if (retry_grasp & nr_grasping_failures < 1)
                        // {
                        //     nr_grasping_failures ++ ;
                        //     automaton_state = PLAN_FOR_RETRACTION;
                        // }
                        // else
                        // {
                        //     nr_grasping_failures = 0;
                        //     automaton_state = MOVE_TO_HOME;
                        // }
                    }
                    else if( automaton_state == PLAN_FOR_BLOCKING_ITEM)
                    {
                        PRX_PRINT("Current State: "<<getStringFromEnum(automaton_state),PRX_TEXT_CYAN);
                        // logic: decide which bin to put; update Json file; planning srv
                        command.stage = 7;
                        command.hand = CurrentArm;

                        // PRX_PRINT("CurrentTarget: "<<CurrentTarget, PRX_TEXT_GREEN);
                        command.object = CurrentTarget;
                        foreach(movable_body_plant_t* plant, movable_bodies)
                        {
                            if(plant->get_object_type() == command.object)
                            {
                                std::vector<double> obj_pose_vector;
                                config_t obj_pose;
                                plant->get_configuration(obj_pose);
                                obj_pose.copy_to_vector(obj_pose_vector);
                                command.object_state = obj_pose_vector;
                            }
                        }
                        
                        std::vector<double> final_obj_state = target_obj_pose;
                        // final_obj_state[0] = 0.97;
                        final_obj_state[1] = 0;
                        // final_obj_state[2] = 1.4;
                        command.final_obj_state = final_obj_state;
                        planning_app_query->sendGoal(command);
                        bool planning_success = false;
                        planning_success = planning_app_query->waitForResult(ros::Duration(0));
                        result = *planning_app_query->getResult();
                        actionlib::SimpleClientGoalState state = planning_app_query->getState();
                        PRX_PRINT("Planning Succeded: "<<state.toString(),PRX_TEXT_RED);
                        PRX_PRINT("Executing Plan..."<<result.duration,PRX_TEXT_CYAN);

                        if (state.toString()=="SUCCEEDED")
                        {
                            current_result = result;
                            automaton_state = EXECUTE_REACHING;
                        }
                    }
                    else if( automaton_state == PLAN_FOR_TARGET_ITEM)
                    {

                        PRX_PRINT("Current State: "<<getStringFromEnum(automaton_state),PRX_TEXT_CYAN);
                        // planning srv

                        PRX_PRINT("simulation_state at beginning of PLAN_FOR_TARGET_ITEM: "<<simulator->get_state_space()->print_point(simulation_state), PRX_TEXT_GREEN);
                        
                        //3 means MOVE_TO_ORDER_BIN (refer to apc_query.action)
                        command.stage = 3;
                        command.hand = CurrentArm;

                        std::vector<double> final_obj_state = target_obj_pose;
                        // final_obj_state[0] = 0.97;
                        final_obj_state[0] = 0.2;
                        final_obj_state[1] = 0;
                        final_obj_state[2] = 0.2;
                        PRX_PRINT("final_obj_state: "<<final_obj_state, PRX_TEXT_GREEN);
                        command.final_obj_state = final_obj_state;

                        planning_app_query->sendGoal(command);
                        bool planning_success = false;
                        planning_app_query->waitForResult(ros::Duration(0));
                        result = *planning_app_query->getResult();
                        actionlib::SimpleClientGoalState state = planning_app_query->getState();
                        PRX_PRINT("Planning Succeded: "<<state.toString(),PRX_TEXT_RED);
                        PRX_PRINT("Executing Plan..."<<result.duration,PRX_TEXT_CYAN);

                        if (state.toString()=="SUCCEEDED")
                        {
                            current_result = result;
                            automaton_state = EXECUTE_PLACING;
                        }
                        else
                        {
                            PRX_PRINT("Cannot find a plan to the tote, disengage EE ...",PRX_TEXT_CYAN);
                            automaton_state = DISENGAGE_EE;
                        }
                    }
                    else if( automaton_state == EXECUTE_PLACING)
                    {
                        PRX_PRINT("---------- EXECUTE_PLACING ---------------------", PRX_TEXT_LIGHTGRAY);
                        PRX_PRINT("CurrentBin: "<<CurrentBin, PRX_TEXT_GREEN);
                        PRX_PRINT("CurrentTarget: "<<CurrentTarget, PRX_TEXT_GREEN);
                        PRX_PRINT("CurrentArm: "<<CurrentArm, PRX_TEXT_GREEN);
                        PRX_PRINT("---------- EXECUTE_PLACING ---------------------",PRX_TEXT_LIGHTGRAY);
                        PRX_PRINT("Current State: "<<getStringFromEnum(automaton_state),PRX_TEXT_CYAN);
                        // send cammand to robot

                        execute_command.plan=current_result.retracting;   
                        // execute_command.plan=current_result.plan;                        

                        controller->convert_and_copy_to_robot_plan(execute_command.plan);
                        controller->set_robot_plan();
                        
                        //ZPP
                        if(real_robot)
                        {
                            create_and_send_robot_command();
                        }

                        plan_duration = current_result.retracting_duration;
                        // plan_duration = current_result.duration;

                        executing_trajectory = true;
                        PRX_PRINT("Executing Plan..."<<plan_duration,PRX_TEXT_CYAN);
                        currently_in_home_position = false;
                        work_order.done[current_item_index] = true;

                        // Update the JSON file
                        update_json_file();
                        output_json_file();
                        clear_plan();
                        automaton_state = TURN_OFF_SENSING;
                    }
                    else if( automaton_state == PLAN_FOR_RETRACTION)
                    {
                        PRX_PRINT("Current State: "<<getStringFromEnum(automaton_state),PRX_TEXT_CYAN);
                        // disengage ee and planning srv w/o object
                        command.stage = 1;
                        command.bin = CurrentBin;
                        command.hand = CurrentArm;
                        
                        double bin_plan_duration = 100;
                        double thresh = 5;
                        bool planning_success = false;
                        int tries_counter = 0;

                        planning_app_query->sendGoal(command);
                        planning_success = planning_app_query->waitForResult(ros::Duration(0));
                        result = *planning_app_query->getResult();
                        // tries_counter++;
                        // if(tries_counter%4==0)
                        // {
                        //     thresh+=.5;
                        // }
                        // if(planning_success && result.plan.size()>0)
                        // {
                        //     bin_plan_duration = result.duration;
                        //     PRX_PRINT("Plan Duration: "<<bin_plan_duration<<"   Bin_Hand: "<<cur_hand<<"_"<<bin_names.at(i),PRX_TEXT_GREEN);
                        // }

                        if(planning_success && result.plan.size()>0)
                        {
                            thresh = 5;
                            PRX_PRINT("Received Plan Successfully...",PRX_TEXT_CYAN);
                            execute_command.plan=result.plan;


                            controller->convert_and_copy_to_robot_plan(execute_command.plan);
                            controller->set_robot_plan();
                            
                            //ZPP
                            if(real_robot)
                            {
                                create_and_send_robot_command();
                            }

                            plan_duration = result.duration;
                            executing_trajectory = true;
                            PRX_PRINT("Executing Plan...",PRX_TEXT_CYAN);
                            currently_in_home_position = false;
                            automaton_state = POSE_ESTIMATION;
                        }
                        else
                        {
                            automaton_state = MOVE_TO_HOME;
                        }

                        
                    }
                    else if( automaton_state == STOP_ROBOT)
                    {
                        PRX_PRINT("Current State: "<<getStringFromEnum(automaton_state),PRX_TEXT_CYAN);                    
                        // decrease object/bin priority
                        //MOVE EVERYTHING FROM THE SHELF BECAUSE OF POSSIBLE COLLISION
                        
                        automaton_state = TURN_OFF_SENSING;
                    }
                    else if( automaton_state == TURN_OFF_SENSING)
                    {
                        PRX_PRINT("Current State: "<<getStringFromEnum(automaton_state),PRX_TEXT_CYAN);
                        
                        automaton_state = MOVE_TO_HOME;
                    }
                    else if( automaton_state == MOVE_TO_HOME)
                    {
                        PRX_PRINT("---------- MOVE_TO_HOME ---------------------", PRX_TEXT_LIGHTGRAY);
                        PRX_PRINT("---------- MOVE_TO_HOME ---------------------",PRX_TEXT_LIGHTGRAY);
                        PRX_PRINT("Current State: "<<getStringFromEnum(automaton_state),PRX_TEXT_CYAN);
                        // reset_object_pose();
                        
                        command.goal_state = home_position;
                        //0 means MOVE (refer to apc_query.action)
                        command.stage = 0;
                        // PRX_PRINT(command.goal_state,PRX_TEXT_BROWN);
                        planning_app_query->sendGoal(command);
                        bool planning_success = false;
                        planning_success = planning_app_query->waitForResult(ros::Duration(0));
                        actionlib::SimpleClientGoalState state = planning_app_query->getState();
                        result = *planning_app_query->getResult();
                        if(state.toString() == "SUCCEEDED" && result.plan.size()>0)
                        {
                            PRX_PRINT("Received Plan Successfully...",PRX_TEXT_CYAN);
                            execute_command.plan=result.plan;
                            controller->convert_and_copy_to_robot_plan(execute_command.plan);
                            controller->set_robot_plan();
                            //ZPP
                            if(real_robot)
                            {
                                create_and_send_robot_command();
                            }
                            


                            plan_duration = result.duration;
                            executing_trajectory = true;
                            PRX_PRINT("Executing Plan...",PRX_TEXT_CYAN);
                            currently_in_home_position = true;
                        }
                        else if (state.toString() == "ABORTED")
                        {
                            PRX_PRINT("Planning failed to provide a plan!",PRX_TEXT_RED);
                        }
                        if(result.plan.size()==0 & state.toString() == "SUCCEEDED")
                        {
                            PRX_PRINT("Already in queried position",PRX_TEXT_CYAN);
                            currently_in_home_position = true;
                        }
                        // planning srv
                        clear_plan();
                        automaton_state = START;
                    }
                    else if( automaton_state == END)
                    {
                        PRX_FATAL_S("Finished every item !");
                    }
                    else{
                        PRX_FATAL_S("Unknown automaton state!");
                    }
                    // controller->send_zero_control();
                    // simulator->get_state_space()->copy_to_point(simulation_state);
                    // PRX_PRINT("simulation_state 4: "<<simulator->get_state_space()->print_point(simulation_state), PRX_TEXT_GREEN);

                    // simulator->propagate(simulation::simulation_step);
                    // simulator->get_state_space()->copy_to_point(simulation_state);
                    // PRX_PRINT("simulation_state 5: "<<simulator->get_state_space()->print_point(simulation_state), PRX_TEXT_GREEN);
                    update_simulation();
                }


                /**
                 * WARNING: This needs to be called at some point for visualization to work. If you override this frame function, you must call this.
                */
                // if(visualization_counter++%visualization_multiple == 0)
                // {
                 tf_broadcasting();
                // }


                //    if ( ground_truth_timer.measure() > ground_truth_time_limit )
                //    {
                //        sim_comm->publish_system_states();
                //        ground_truth_timer.reset();
                //    }

             }

             double apc_sim_application_t::l2_norm(std::vector<double> const& u) 
             {
                double accum = 0.;
                for (double x : u) {
                    accum += x * x;
                }
                return sqrt(accum);
            }

            void apc_sim_application_t::update_simulation()
            {
                if(!real_robot)
                {
                    // PRX_PRINT("Updating Simulation",PRX_TEXT_CYAN);
                    simulator->push_control(simulation_control);
                    simulator->propagate_and_respond();
                    simulation_state_space->copy_to_point(simulation_state);
                    loop_total += loop_timer.measure_reset();
                    loop_counter++;
                    loop_avg = loop_total / loop_counter;
                    publish_state();
                }
                else
                {
                    controller->send_zero_control();
                    // simulator->get_state_space()->copy_to_point(simulation_state);
                    // PRX_PRINT("simulation_state 4: "<<simulator->get_state_space()->print_point(simulation_state), PRX_TEXT_GREEN);

                    simulator->propagate(simulation::simulation_step);
                    // simulator->get_state_space()->copy_to_point(simulation_state);
                    // PRX_PRINT("simulation_state 5: "<<simulator->get_state_space()->print_point(simulation_state), PRX_TEXT_GREEN);
                    // update_simulation();
                    // control_t* zero_control = simulation_control_space->alloc_point();
                    // simulator->push_control(zero_control);
                    // simulator->propagate_and_respond();
                    // simulation_state_space->copy_to_point(simulation_state);
                    publish_state();
                }
            }

            void apc_sim_application_t::plan_to_bin(prx_planning::apc_queryGoal command){}

            void apc_sim_application_t::estimate_shelf_position()
            {
#ifdef PRX_SENSING_FOUND

                ros::ServiceClient prx_sensing_find_shelf_cl = n.serviceClient<prx_sensing::UpdateShelfPosition>("prx/sensing/update_shelf_pose");
                prx_sensing_find_shelf_cl.waitForExistence(ros::Duration(-1.0));
                prx_sensing::UpdateShelfPosition srv_shelf;
                srv_shelf.request.update_shelf = true;
                prx_sensing_find_shelf_cl.call(srv_shelf);

                config_t new_config;
                PRX_PRINT("Inside shelf estimate_shelf_position",PRX_TEXT_CYAN);
                // TODO: find shelf, place shelf
                foreach(sim::obstacle_t* obst, obstacles)
                {
                    // PRX_PRINT(obstacles.get_pathname(),PRX_TEXT_RED);
                    PRX_PRINT("Obstacle = "<<obst->get_pathname(),PRX_TEXT_GREEN);
                    if(obst->get_pathname().find("shelf")!=std::string::npos) 
                    {
                        shelf = obst;
                        PRX_PRINT("Found the Shelf"<<shelf->get_root_configuration().print(),PRX_TEXT_RED);
                        double x,y,z,qx,qy,qz,qw;
                        x = srv_shelf.response.shelf_pose.pose.position.x;
                        y = srv_shelf.response.shelf_pose.pose.position.y;
                        z = srv_shelf.response.shelf_pose.pose.position.z;
                        qx = srv_shelf.response.shelf_pose.pose.orientation.x;
                        qy = srv_shelf.response.shelf_pose.pose.orientation.y;
                        qz = srv_shelf.response.shelf_pose.pose.orientation.z;
                        qw = srv_shelf.response.shelf_pose.pose.orientation.w;
                        new_config.set_position(x,y,z);
                        new_config.set_orientation(qx,qy,qz,qw);
                        shelf->update_root_configuration(new_config);
                        shelf->update_collision_info();

                        //send shelf pose to planning
                        objectMsg.name = "shelf";
                        std::vector<double> shelf_pose_vec;
                        new_config.get_position(shelf_pose_vec);
                        shelf_pose_vec.push_back(qx);
                        shelf_pose_vec.push_back(qy);
                        shelf_pose_vec.push_back(qz);
                        shelf_pose_vec.push_back(qw);
                        objectMsg.elements = shelf_pose_vec;
                        object_publisher.publish(objectMsg);
                        PRX_PRINT("Updated the shelf's position: "<<shelf->get_root_configuration().print(),PRX_TEXT_RED);
                        shelf = NULL;
                    } 
                }
#endif
            }

            void apc_sim_application_t::estimate_objects_pose()
            {

                std::vector<double> dummy_state;
                dummy_state.push_back(3);
                dummy_state.push_back(3);
                dummy_state.push_back(3);
                dummy_state.push_back(0);
                dummy_state.push_back(0);
                dummy_state.push_back(0);
                dummy_state.push_back(1);

                // #ifdef PRX_SENSING_FOUND
                PRX_PRINT("Found PRX Sensing...",PRX_TEXT_GREEN);

                //switch camera
                ros::ServiceClient prx_switch_camera_cl = n.serviceClient<prx_sensing::SwitchCameras>("/prx/sensing/switch_cameras");
                prx_sensing::SwitchCameras switch_camera_srv;
                if (CurrentArm == "left")
                {
                    switch_camera_srv.request.camera_index = "left_kinect";
                }
                else
                {
                    switch_camera_srv.request.camera_index = "right_kinect";
                }
                prx_switch_camera_cl.call(switch_camera_srv);


                // Testing calling services advertised by prx_sensing
                ros::ServiceClient prx_sensing_update_cl = n.serviceClient<prx_sensing::UpdateObjectList>("prx/sensing/update_object_list");

                prx_sensing::UpdateObjectList sensing_update_srv;

                for (int i = 0; i < 10; ++i)
                {   
                //     std::string temp = "bin_";
                //     std::string bin_name = work_order.bin[i].substr(work_order.bin[i].length() - 1, 1);
                //     temp += bin_name;
                    // PRX_PRINT("current_item_index: "<<i, PRX_TEXT_GREEN);
                    switch(CurrentBin[0]){
                        case 'A':
                        if (bin_contents.bin_A[i].size()>0)
                        {
                            if (bin_contents.bin_A[i] == "creativity_chenille_stems")
                            {
                                sensing_update_srv.request.object_list.push_back("creativity_chenille_stems_1");
                            }
                            else{ sensing_update_srv.request.object_list.push_back(bin_contents.bin_A[i]);}                            
                            sensing_update_srv.request.bin_id = 1;
                        }
                        break;
                        case 'B':
                        if (bin_contents.bin_B[i].size()>0)
                        {
                            if (bin_contents.bin_B[i] == "creativity_chenille_stems")
                            {
                                sensing_update_srv.request.object_list.push_back("creativity_chenille_stems_1");
                            }
                            else{ sensing_update_srv.request.object_list.push_back(bin_contents.bin_B[i]);}
                            sensing_update_srv.request.bin_id = 2;
                        }
                        break;
                        case 'C':
                        if (bin_contents.bin_C[i].size()>0)
                        {
                            if (bin_contents.bin_C[i] == "creativity_chenille_stems")
                            {
                                sensing_update_srv.request.object_list.push_back("creativity_chenille_stems_1");
                            }
                            else{ sensing_update_srv.request.object_list.push_back(bin_contents.bin_C[i]);}
                            sensing_update_srv.request.bin_id = 3;
                        }
                        break;
                        case 'D':
                        if (bin_contents.bin_D[i].size()>0)
                        {
                            if (bin_contents.bin_D[i] == "creativity_chenille_stems")
                            {
                                sensing_update_srv.request.object_list.push_back("creativity_chenille_stems_1");
                            }
                            else{ sensing_update_srv.request.object_list.push_back(bin_contents.bin_D[i]);}
                            sensing_update_srv.request.bin_id = 4;
                        }
                        break;
                        case 'E':
                        if (bin_contents.bin_E[i].size()>0)
                        {
                            if (bin_contents.bin_E[i] == "creativity_chenille_stems")
                            {
                                sensing_update_srv.request.object_list.push_back("creativity_chenille_stems_1");
                            }
                            else{ sensing_update_srv.request.object_list.push_back(bin_contents.bin_E[i]);}
                            sensing_update_srv.request.bin_id = 5;
                        }
                        break;
                        case 'F':
                        if (bin_contents.bin_F[i].size()>0)
                        {
                            if (bin_contents.bin_F[i] == "creativity_chenille_stems")
                            {
                                sensing_update_srv.request.object_list.push_back("creativity_chenille_stems_1");
                            }
                            else{sensing_update_srv.request.object_list.push_back(bin_contents.bin_F[i]);}
                            sensing_update_srv.request.bin_id = 6;
                        }
                        break;
                        case 'G':
                        if (bin_contents.bin_G[i].size()>0)
                        {
                            if (bin_contents.bin_G[i] == "creativity_chenille_stems")
                            {
                                sensing_update_srv.request.object_list.push_back("creativity_chenille_stems_1");
                            }
                            else{ sensing_update_srv.request.object_list.push_back(bin_contents.bin_G[i]);}
                            sensing_update_srv.request.bin_id = 7;
                        }
                        break;
                        case 'H':
                        if (bin_contents.bin_H[i].size()>0)
                        {
                            if (bin_contents.bin_H[i] == "creativity_chenille_stems")
                            {
                                sensing_update_srv.request.object_list.push_back("creativity_chenille_stems_1");
                            }
                            else{ sensing_update_srv.request.object_list.push_back(bin_contents.bin_H[i]); }
                            sensing_update_srv.request.bin_id = 8;
                        }
                        break;
                        case 'I':
                        if (bin_contents.bin_I[i].size()>0)
                        {
                            if (bin_contents.bin_I[i] == "creativity_chenille_stems")
                            {
                                sensing_update_srv.request.object_list.push_back("creativity_chenille_stems_1");
                            }
                            else{ sensing_update_srv.request.object_list.push_back(bin_contents.bin_I[i]); }
                            sensing_update_srv.request.bin_id = 9;
                        }
                        break;
                        case 'J':
                        if (bin_contents.bin_J[i].size()>0)
                        {
                            if (bin_contents.bin_J[i] == "creativity_chenille_stems")
                            {
                                sensing_update_srv.request.object_list.push_back("creativity_chenille_stems_1");
                            }
                            else{ sensing_update_srv.request.object_list.push_back(bin_contents.bin_J[i]); }
                            sensing_update_srv.request.bin_id = 10;
                        }
                        break;
                        case 'K':
                        if (bin_contents.bin_K[i].size()>0)
                        {
                            if (bin_contents.bin_K[i] == "creativity_chenille_stems")
                            {
                                sensing_update_srv.request.object_list.push_back("creativity_chenille_stems_1");
                            }
                            else{ sensing_update_srv.request.object_list.push_back(bin_contents.bin_K[i]); }
                            sensing_update_srv.request.bin_id = 11;
                        }
                        break;
                        case 'L':
                        if (bin_contents.bin_L[i].size()>0)
                        {
                            if (bin_contents.bin_L[i] == "creativity_chenille_stems")
                            {
                                sensing_update_srv.request.object_list.push_back("creativity_chenille_stems_1");
                            }
                            else{ sensing_update_srv.request.object_list.push_back(bin_contents.bin_L[i]); }
                            sensing_update_srv.request.bin_id = 12;
                        }
                        break;         
                    }
                }
                    // sensing_update_srv.request.object_list.push_back("ticonderoga_12_pencils");
                    // sensing_update_srv.request.object_list.push_back("crayola_24_ct");
                    // sensing_update_srv.request.object_list.push_back("expo_dry_erase_board_eraser");
                    // sensing_update_srv.request.object_list.push_back("i_am_a_bunny_book");
                    // sensing_update_srv.request.object_list.push_back("dove_beauty_bar");
                    // sensing_update_srv.request.object_list.push_back("soft_white_lightbulb");
                    // sensing_update_srv.request.object_list.push_back("kleenex_tissue_box");
                    // sensing_update_srv.request.object_list.push_back("laugh_out_loud_joke_book");
                    // sensing_update_srv.request.object_list.push_back("staples_index_cards");
                // sensing_update_srv.request.object_list.push_back("cheezit_big_original");

                prx_sensing_update_cl.call(sensing_update_srv);
                PRX_INFO_S(sensing_update_srv.response);

//                 moved = NULL;
// // #endif 

//                 ros::ServiceClient prx_sensing_publish_cl = n.serviceClient<prx_sensing::PublishObjectList>("prx/sensing/publish_object_list");

//                 prx_sensing::PublishObjectList srv;
//                 srv.request.publish_objects = true;
//                 prx_sensing_publish_cl.call(srv);
            }

            void apc_sim_application_t::output_json_file()
            {
                std::ofstream fout;

                fout.open(output_json_path);

                fout << "{\n";
                fout << "\t\"bin_contents\":{\n";

                std::vector<char> alphabet = {'A','B','C','D','E','F','G','H','I','J','K','L'};
                for (unsigned i = 0; i < 12; ++i)
                {
                    fout << "\t\t\"bin_" << alphabet[i] << "\":[ \n";
                    for (unsigned j = 0; j < 10; ++j)
                    {
                        switch(alphabet[i])
                        {
                            case 'A':
                            if (bin_contents.bin_A[j].size()>0)
                            {
                                fout << "\t\t\t\"" << bin_contents.bin_A[j] << "\",\n";
                            }
                            break;
                            case 'B':
                            if (bin_contents.bin_B[j].size()>0)
                            {
                                fout << "\t\t\t\"" << bin_contents.bin_B[j] << "\",\n";
                            }
                            break;
                            case 'C':
                            if (bin_contents.bin_C[j].size()>0)
                            {
                                fout << "\t\t\t\"" << bin_contents.bin_C[j] << "\",\n";
                            }
                            break;
                            case 'D':
                            if (bin_contents.bin_D[j].size()>0)
                            {
                                fout << "\t\t\t\"" << bin_contents.bin_D[j] << "\",\n";
                            }
                            break;
                            case 'E':
                            if (bin_contents.bin_E[j].size()>0)
                            {
                                fout << "\t\t\t\"" << bin_contents.bin_E[j] << "\",\n";
                            }
                            break;
                            case 'F':
                            if (bin_contents.bin_F[j].size()>0)
                            {
                                fout << "\t\t\t\"" << bin_contents.bin_F[j] << "\",\n";
                            }
                            break;
                            case 'G':
                            if (bin_contents.bin_G[j].size()>0)
                            {
                                fout << "\t\t\t\"" << bin_contents.bin_G[j] << "\",\n";
                            }
                            break;
                            case 'H':
                            if (bin_contents.bin_H[j].size()>0)
                            {
                                fout << "\t\t\t\"" << bin_contents.bin_H[j] << "\",\n";
                            }
                            break;
                            case 'I':
                            if (bin_contents.bin_I[j].size()>0)
                            {
                                fout << "\t\t\t\"" << bin_contents.bin_I[j] << "\",\n";
                            }
                            break;
                            case 'J':
                            if (bin_contents.bin_J[j].size()>0)
                            {
                                fout << "\t\t\t\"" << bin_contents.bin_J[j] << "\",\n";
                            }
                            break;
                            case 'K':
                            if (bin_contents.bin_K[j].size()>0)
                            {
                                fout << "\t\t\t\"" << bin_contents.bin_K[j] << "\",\n";
                            }
                            break;
                            case 'L':
                            if (bin_contents.bin_L[j].size()>0)
                            {
                                fout << "\t\t\t\"" << bin_contents.bin_L[j] << "\",\n";
                            }
                            break;  
                        }
                    }

                    fout << "\t\t],\n";
                }
                fout << "\t},\n";

                fout << "\t\"tote_contents\": [\n";

                for (auto tote_item : tote_contents)
                {
                    fout << "\t\t\"" << tote_item << "\",\n";
                }

                fout << "\t]\n}";
                fout.close();

            }
            void apc_sim_application_t::update_json_file()
            {
                bool updated_item = false;

                // Step 1: Find the item in the correct bin and remove it
                for (int i = 0; i < 10 && !updated_item; ++i)
                {
                    switch(CurrentBin[0])
                    {
                        case 'A':
                        if (bin_contents.bin_A[i].size()>0)
                        {
                            if (bin_contents.bin_A[i] == CurrentTarget)
                            {
                                bin_contents.bin_A[i] = "";
                                tote_contents.push_back(CurrentTarget);
                                updated_item = true;
                            }
                        }
                        break;
                        case 'B':
                        if (bin_contents.bin_B[i].size()>0)
                        {
                            if (bin_contents.bin_B[i] == CurrentTarget)
                            {
                                bin_contents.bin_B[i] = "";
                                tote_contents.push_back(CurrentTarget);
                                updated_item = true;
                            }
                        }
                        break;
                        case 'C':
                        if (bin_contents.bin_C[i].size()>0)
                        {
                            if (bin_contents.bin_C[i] == CurrentTarget)
                            {
                                bin_contents.bin_C[i] = "";
                                tote_contents.push_back(CurrentTarget);
                                updated_item = true;
                            }
                        }
                        break;
                        case 'D':
                        if (bin_contents.bin_D[i].size()>0)
                        {
                            if (bin_contents.bin_D[i] == CurrentTarget)
                            {
                                bin_contents.bin_D[i] = "";
                                tote_contents.push_back(CurrentTarget);
                                updated_item = true;
                            }
                        }
                        break;
                        case 'E':
                        if (bin_contents.bin_E[i].size()>0)
                        {
                            if (bin_contents.bin_E[i] == CurrentTarget)
                            {
                                bin_contents.bin_E[i] = "";
                                tote_contents.push_back(CurrentTarget);
                                updated_item = true;
                            }
                        }
                        break;
                        case 'F':
                        if (bin_contents.bin_F[i].size()>0)
                        {
                            if (bin_contents.bin_F[i] == CurrentTarget)
                            {
                                bin_contents.bin_F[i] = "";
                                tote_contents.push_back(CurrentTarget);
                                updated_item = true;
                            }
                        }
                        break;
                        case 'G':
                        if (bin_contents.bin_G[i].size()>0)
                        {
                            if (bin_contents.bin_G[i] == CurrentTarget)
                            {
                                bin_contents.bin_G[i] = "";
                                tote_contents.push_back(CurrentTarget);
                                updated_item = true;
                            }
                        }
                        break;
                        case 'H':
                        if (bin_contents.bin_H[i].size()>0)
                        {
                            if (bin_contents.bin_H[i] == CurrentTarget)
                            {
                                bin_contents.bin_H[i] = "";
                                tote_contents.push_back(CurrentTarget);
                                updated_item = true;
                            }
                        }
                        break;
                        case 'I':
                        if (bin_contents.bin_I[i].size()>0)
                        {
                            if (bin_contents.bin_I[i] == CurrentTarget)
                            {
                                bin_contents.bin_I[i] = "";
                                tote_contents.push_back(CurrentTarget);
                                updated_item = true;
                            }
                        }
                        break;
                        case 'J':
                        if (bin_contents.bin_J[i].size()>0)
                        {
                            if (bin_contents.bin_J[i] == CurrentTarget)
                            {
                                bin_contents.bin_J[i] = "";
                                tote_contents.push_back(CurrentTarget);
                                updated_item = true;
                            }
                        }
                        break;
                        case 'K':
                        if (bin_contents.bin_K[i].size()>0)
                        {
                            if (bin_contents.bin_K[i] == CurrentTarget)
                            {
                                bin_contents.bin_K[i] = "";
                                tote_contents.push_back(CurrentTarget);
                                updated_item = true;
                            }
                        }
                        break;
                        case 'L':
                        if (bin_contents.bin_L[i].size()>0)
                        {
                            if (bin_contents.bin_L[i] == CurrentTarget)
                            {
                                bin_contents.bin_L[i] = "";
                                tote_contents.push_back(CurrentTarget);
                                updated_item = true;
                            }
                        }
                        break;         
                    }
                }

                if (!updated_item)
                {
                    PRX_ERROR_S ("Could not update item: " << CurrentTarget << " for Bin " << CurrentBin);
                }
            }
            // FALSE: COLLISION, TRUE: NO COLLISION 
            bool apc_sim_application_t::check_object_collision(movable_body_plant_t* plant, unsigned depth)
            {
                // foreach(collision_pair_t cp, simulator->get_colliding_bodies()->get_body_pairs())
                // {
                //     PRX_PRINT("ALL collision Pair: " << cp.first <<", " << cp.second, PRX_TEXT_BROWN);
                //     PRX_PRINT("z_axis_offset: "<<z_axis_offset,PRX_TEXT_CYAN);
                //     PRX_PRINT("left_offset: "<<left_offset,PRX_TEXT_CYAN);
                //     PRX_PRINT("right_offset: "<<right_offset,PRX_TEXT_CYAN);
                // }

                bool left = true, right = true, top = true;

                if (z_axis_offset >= offset_threshold || left_offset >= offset_threshold || right_offset >= offset_threshold)
                {
                    z_axis_offset = 0.0;
                    left_offset = 0.0;
                    right_offset = 0.0;
                    return false;
                }
                bool collision_with_bin_bottom = false;
                bool collision_with_left_side = false;
                bool collision_with_right_side = false;
                if (z_axis_offset < offset_threshold)
                {
                    //PRX_PRINT("Check collision with bottom", PRX_TEXT_CYAN);
                    foreach(collision_pair_t cp, simulator->get_colliding_bodies()->get_body_pairs())
                    {
                        collision_with_bin_bottom = ( cp.first.find(CurrentTarget)!=std::string::npos) && (cp.second.find("top_shelf")!=std::string::npos 
                            || cp.second.find("middle_shelf")!=std::string::npos || cp.second.find("bottom_shelf")!=std::string::npos || cp.second.find("bottom")!=std::string::npos ) 
                            || ( cp.second.find(CurrentTarget)!=std::string::npos) && (cp.first.find("top_shelf")!=std::string::npos 
                            || cp.first.find("middle_shelf")!=std::string::npos || cp.first.find("bottom_shelf")!=std::string::npos || cp.first.find("bottom")!=std::string::npos );
                        if( collision_with_bin_bottom )
                        {
                            //PRX_PRINT("Collision with bottom! "<< cp.first <<", " << cp.second, PRX_TEXT_BROWN);
                            //PRX_PRINT("target_obj_pose[2]"<<target_obj_pose[2], PRX_TEXT_CYAN);
                            config_t obj_pose;
                            // z_axis_offset += 0.005; 
                            // target_obj_pose[2] += 0.005;
                            z_axis_offset += 0.01; 
                            target_obj_pose[2] += 0.01;
                            obj_pose.set_position(target_obj_pose[0],target_obj_pose[1],target_obj_pose[2]);
                            obj_pose.set_orientation(target_obj_pose[3],target_obj_pose[4],target_obj_pose[5],target_obj_pose[6]);
                            // target_obj_pose;
                            plant->set_configuration(obj_pose);
                            plant->update_collision_info();
                            if (!real_robot || no_sensing)
                                update_simulation();
                            top = check_object_collision(plant, depth + 1);
                        }    
                    }        
                }
                if ( depth == 0 && top && collision_with_bin_bottom)
                {
                    config_t obj_pose;
                    target_obj_pose[2] += 0.01;
                    obj_pose.set_position(target_obj_pose[0],target_obj_pose[1],target_obj_pose[2]);
                    obj_pose.set_orientation(target_obj_pose[3],target_obj_pose[4],target_obj_pose[5],target_obj_pose[6]);
                    // target_obj_pose;
                    plant->set_configuration(obj_pose);
                    plant->update_collision_info();

                }

                if (left_offset < offset_threshold)
                {
                    //PRX_PRINT("Check collision with left", PRX_TEXT_CYAN);
                    foreach (collision_pair_t cp, simulator->get_colliding_bodies()->get_body_pairs())
                    {
                        if (CurrentBin == "A" || CurrentBin == "D" || CurrentBin == "G" || CurrentBin == "J")
                        {
                            //PRX_PRINT("Collision pair:  "<< cp.first <<", " << cp.second, PRX_TEXT_BROWN);
                            collision_with_left_side = ( cp.first.find(CurrentTarget)!=std::string::npos) && (cp.second.find("left_side")!=std::string::npos ) 
                            || ( cp.second.find(CurrentTarget)!=std::string::npos) && (cp.first.find("left_side")!=std::string::npos );
                        }
                        else if (CurrentBin == "B" || CurrentBin == "E" || CurrentBin == "H" || CurrentBin == "K")
                        {
                            //PRX_PRINT("Collision pair:  "<< cp.first <<", " << cp.second, PRX_TEXT_BROWN);
                            collision_with_left_side = ( cp.first.find(CurrentTarget)!=std::string::npos) && (cp.second.find("left_divider")!=std::string::npos )
                            || ( cp.second.find(CurrentTarget)!=std::string::npos) && (cp.first.find("left_divider")!=std::string::npos );
                        }
                        else if (CurrentBin == "C" || CurrentBin == "F" || CurrentBin == "I" || CurrentBin == "L")
                        {
                            //PRX_PRINT("Collision pair:  "<< cp.first <<", " << cp.second, PRX_TEXT_BROWN);
                            collision_with_left_side = ( cp.first.find(CurrentTarget)!=std::string::npos) && (cp.second.find("right_divider")!=std::string::npos )
                            || ( cp.second.find(CurrentTarget)!=std::string::npos) && (cp.first.find("left_divider")!=std::string::npos );
                        }

                        if (collision_with_left_side)
                        {
                            //PRX_PRINT("Collision with left! " << cp.first <<", " << cp.second, PRX_TEXT_BROWN);
                            //PRX_PRINT("target_obj_pose[2]"<<target_obj_pose[1], PRX_TEXT_CYAN);
                            config_t obj_pose;
                            // left_offset += 0.005; 
                            // target_obj_pose[1] -= 0.005;
                            left_offset += 0.01; 
                            target_obj_pose[1] -= 0.01;
                            obj_pose.set_position(target_obj_pose[0],target_obj_pose[1],target_obj_pose[2]);
                            obj_pose.set_orientation(target_obj_pose[3],target_obj_pose[4],target_obj_pose[5],target_obj_pose[6]);
                            plant->set_configuration(obj_pose);
                            plant->update_collision_info();
                            if (!real_robot || no_sensing)
                                update_simulation();
                            left = check_object_collision(plant, depth + 1);
                        }    
                    }    
                }
                if ( depth == 0 && left && collision_with_left_side)
                {
                    config_t obj_pose;
                    target_obj_pose[1] -= 0.01;
                    obj_pose.set_position(target_obj_pose[0],target_obj_pose[1],target_obj_pose[2]);
                    obj_pose.set_orientation(target_obj_pose[3],target_obj_pose[4],target_obj_pose[5],target_obj_pose[6]);
                    plant->set_configuration(obj_pose);
                    plant->update_collision_info();
                }

                if (right_offset < offset_threshold)
                {
//                    PRX_PRINT("Check collision with right", PRX_TEXT_CYAN);
                    foreach(collision_pair_t cp, simulator->get_colliding_bodies()->get_body_pairs())
                    {
                        if (CurrentBin == "A" || CurrentBin == "D" || CurrentBin == "G" || CurrentBin == "J")
                        {
                            //PRX_PRINT("Collision pair:  "<< cp.first <<", " << cp.second, PRX_TEXT_BROWN);
                            collision_with_right_side = ( cp.first.find(CurrentTarget)!=std::string::npos) && (cp.second.find("left_divider")!=std::string::npos )
                            || ( cp.second.find(CurrentTarget)!=std::string::npos) && (cp.first.find("left_divider")!=std::string::npos );
                        }
                        
                        else if (CurrentBin == "B" || CurrentBin == "E" || CurrentBin == "H" || CurrentBin == "K")
                        {
                            //PRX_PRINT("Collision pair:  "<< cp.first <<", " << cp.second, PRX_TEXT_BROWN);
                            collision_with_right_side = ( cp.first.find(CurrentTarget)!=std::string::npos) && (cp.second.find("right_divider")!=std::string::npos )
                            || ( cp.second.find(CurrentTarget)!=std::string::npos) && (cp.first.find("right_divider")!=std::string::npos );
                        }
                        
                        else if (CurrentBin == "C" || CurrentBin == "F" || CurrentBin == "I" || CurrentBin == "L")
                        {
                            //PRX_PRINT("Collision pair:  "<< cp.first <<", " << cp.second, PRX_TEXT_BROWN);
                            collision_with_right_side = ( cp.first.find(CurrentTarget)!=std::string::npos) && (cp.second.find("right_side")!=std::string::npos )
                            || ( cp.second.find(CurrentTarget)!=std::string::npos) && (cp.first.find("right_side")!=std::string::npos );
                        }
                        

                        if (collision_with_right_side)
                        {
                            //PRX_PRINT("Collision Pair: " << cp.first <<", " << cp.second, PRX_TEXT_BROWN);
                            //PRX_PRINT("target_obj_pose[2]"<<target_obj_pose[1], PRX_TEXT_CYAN);
                            config_t obj_pose;
                            // right_offset += 0.005; 
                            // target_obj_pose[1] += 0.005;
                            right_offset += 0.01; 
                            target_obj_pose[1] += 0.01;
                            obj_pose.set_position(target_obj_pose[0],target_obj_pose[1],target_obj_pose[2]);
                            obj_pose.set_orientation(target_obj_pose[3],target_obj_pose[4],target_obj_pose[5],target_obj_pose[6]);
                            plant->set_configuration(obj_pose);
                            plant->update_collision_info();
                            if (!real_robot || no_sensing)
                                update_simulation();
                            right = check_object_collision(plant, depth + 1);
                        }    
                    }
                }
                if ( depth == 0 && right && collision_with_right_side)
                {
                    config_t obj_pose;
                    target_obj_pose[1] += 0.01;
                    obj_pose.set_position(target_obj_pose[0],target_obj_pose[1],target_obj_pose[2]);
                    obj_pose.set_orientation(target_obj_pose[3],target_obj_pose[4],target_obj_pose[5],target_obj_pose[6]);
                    plant->set_configuration(obj_pose);
                    plant->update_collision_info();
                }

                z_axis_offset = 0.0;
                left_offset = 0.0;
                right_offset = 0.0;

                return (left && right && top);
            }


            bool apc_sim_application_t::update_objects_pose()
            {
                sleep(3);
                moved = NULL;

                ros::ServiceClient prx_sensing_publish_cl = n.serviceClient<prx_sensing::PublishObjectList>("prx/sensing/publish_object_list");

                prx_sensing::PublishObjectList srv;
                srv.request.publish_objects = true;
                prx_sensing_publish_cl.call(srv);
                
                bool found_target = false;
                PRX_PRINT("\n\n\nUPDATE OBJECTS POSE\n",PRX_TEXT_GREEN);
                foreach(prx_sensing::ObjectPose& obj, srv.response.object_list)
                {   
                    std::string object_name = obj.object_name;
                    config_t new_config;

                    PRX_PRINT("MESSAGE Object = "<<object_name,PRX_TEXT_RED);
                    if(object_name == CurrentTarget)
                    {
                        PRX_PRINT("*SIMULATION* found_target = "<<CurrentTarget,PRX_TEXT_MAGENTA);
                        found_target = true;
                    }

                    foreach(movable_body_plant_t* plant, movable_bodies)
                    {
                       // PRX_PRINT("SIMULATION Object = "<<plant->get_object_type(),PRX_TEXT_GREEN);
                        if(plant->get_object_type() == object_name)
                        {
                            PRX_PRINT("*SIMULATION* Object = "<<plant->get_object_type(),PRX_TEXT_GREEN);
                            moved = plant;
                            double x,y,z,qx,qy,qz,qw;
                            x = obj.pose.pose.position.x;
                            y = obj.pose.pose.position.y;
                            z = obj.pose.pose.position.z;
                            qx = obj.pose.pose.orientation.x;
                            qy = obj.pose.pose.orientation.y;
                            qz = obj.pose.pose.orientation.z;
                            qw = obj.pose.pose.orientation.w;
                            new_config.set_position(x,y,z);
                            new_config.set_orientation(qx,qy,qz,qw);
                        // PRX_PRINT("base: "<<base_config.print()<<"new: "<<new_config,PRX_TEXT_GREEN);
                            moved->set_configuration(new_config);
                            moved->update_collision_info();
                            new_config.copy_to_vector(target_obj_pose);

                            if(!check_object_collision(moved, 0))
                            {
                                if(plant->get_object_type() == CurrentTarget)
                                {
                                    PRX_PRINT ("TARGET ITEM IN COLLISION. TERMINATE\n\n", PRX_TEXT_RED);
                                    return false;
                                }
                                else
                                {
                                    PRX_PRINT ("Tried to move object : " << object_name << " out of collision. FAIL", PRX_TEXT_RED);
                                }
                            }
                            else
                            {
                                PRX_PRINT ("Tried to move object : " << object_name << " out of collision. SUCCEED", PRX_TEXT_LIGHTGRAY);
                            }

                            PRX_PRINT ("PUBLISH OBJECT POSE: " << object_name,  PRX_TEXT_BLUE);
                        //send object poses to planning
                            objectMsg.name = object_name;
                            // std::vector<double> pose_vec;
                            // new_config.get_position(pose_vec);
                            // pose_vec.push_back(qx);
                            // pose_vec.push_back(qy);
                            // pose_vec.push_back(qz);
                            // pose_vec.push_back(qw);
                            objectMsg.elements = target_obj_pose;
                            object_publisher.publish(objectMsg);
                            moved->print_configuration();
                            moved = NULL;
                        }

                        // else
                        // {
                        //     plant->get_state_space()->set_from_vector(dummy_state);
                        //     //send object poses to planning
                        //     objectMsg.name = plant->get_object_type();
                        //     objectMsg.elements = dummy_state;
                        //     object_publisher.publish(objectMsg);
                        //     // plant->print_configuration();
                        //     dummy_state[0]+=2;
                        //     dummy_state[1]+=2;
                        //     dummy_state[2]+=2;  
                        //     // PRX_INFO_S("*SIMULATION*: Resetting "<<plant->get_object_type());
                        // }
                        }
                    }

                // ros::NodeHandle sensing_node;
                // std::vector<double> current_target_obj_pose;
                // std::string topic = "/simtrack/"+CurrentTarget;
                // PRX_PRINT("topic: "<<topic, PRX_TEXT_CYAN);
                // get_current_target_obj_pose = sensing_node.subscribe<geometry_msgs::PoseStamped>(topic,100,&apc_sim_application_t::get_current_target_pose_callback, this);
                    PRX_PRINT("found_target: "<<found_target, PRX_TEXT_GREEN);
                    return found_target;
                }

                void apc_sim_application_t::place_object_callback(const prx_simulation::object_msg& objectMsg)
                {
                    config_t new_config;
                    foreach(movable_body_plant_t* plant, movable_bodies)
                    {
                    // PRX_INFO_S("Try Setting "<<objectMsg.name);
                        if(plant->get_object_type() == objectMsg.name)
                        {
                            PRX_INFO_S("Setting "<<plant->get_object_type());
                            moved = plant;
                            new_config.set_position(objectMsg.elements[0],objectMsg.elements[1],objectMsg.elements[2]);
                            new_config.set_orientation(objectMsg.elements[3],objectMsg.elements[4],objectMsg.elements[5],objectMsg.elements[6]);
                            moved->set_configuration(new_config);
                            moved = NULL;
                        }
                    }
                }

                void apc_sim_application_t::reset_object_pose()
                {
                // ros::ServiceClient prx_sensing_publish_cl = n.serviceClient<prx_sensing::UpdateObjectList>("prx/sensing/update_object_list");

                // prx_sensing::UpdateObjectList srv;
                // // srv.request.publish_objects = true;
                // prx_sensing_publish_cl.call(srv);

                    std::vector<double> dummy_state;
                    dummy_state.push_back(3);
                    dummy_state.push_back(3);
                    dummy_state.push_back(3);
                    dummy_state.push_back(0);
                    dummy_state.push_back(0);
                    dummy_state.push_back(0);
                    dummy_state.push_back(1);

                    foreach(movable_body_plant_t* obj, movable_bodies)
                    {
                        obj->get_state_space()->set_from_vector(dummy_state);
                    //send object poses to planning
                        objectMsg.name = obj->get_object_type();
                        objectMsg.elements = dummy_state;
                        object_publisher.publish(objectMsg);
                    // obj->print_configuration();
                        dummy_state[0]+=1;
                        dummy_state[1]+=1;
                        dummy_state[2]+=1;  
                    // PRX_INFO_S("*SIMULATION*: Resetting "<<obj->get_object_type());
                    }
                }

                void apc_sim_application_t::set_selected_path(const std::string& path){}

                void apc_sim_application_t::create_and_send_robot_command()
                {
                    std::vector<trajectory_t*> robot_trajs;
                    std::vector<plan_t*> robot_plans;
                    std::vector<bool> grasp_plan;

                    controller->get_robot_plans(robot_trajs, robot_plans, grasp_plan);
                    for(unsigned index = 0;index<robot_trajs.size();index++)
                    {
                        double duration = create_robot_message(robot_trajs, robot_plans, grasp_plan, index);
                        if(duration>0){
                            PRX_INFO_S("Sending robot message");
                            send_robot_message(duration);
                        }
                    }
                    PRX_INFO_S("Done");
                    clear_plan();
                }

                double apc_sim_application_t::create_robot_message(std::vector<trajectory_t*>& robot_trajs, std::vector<plan_t*>& robot_plans, std::vector<bool>& grasp_plan, int index)
                {
                    state_t* local_state = child_state_space->alloc_point();
                    double duration=0;
                    if(!grasp_plan[index] && robot_plans[index]->length()>0)
                    {
                        PRX_INFO_S("Plan: \n"<<robot_plans[index]->length());
                        child_state_space->copy_to_point(local_state);
                        robot_trajs[index]->copy_onto_back(local_state);

                        block();
                        foreach(plan_step_t step, *robot_plans[index])
                        {
                            // If there is at least one step
                            if (step.duration >= (0.02 - PRX_ZERO_CHECK) )
                            {    
                                int steps = (int)((step.duration / simulation::simulation_step) + .1);
                                output_control_space->copy_from_point(step.control);
                                for(int i=0;i<steps;i++)
                                {
                                    controller->propagate(simulation::simulation_step);
                                    child_state_space->copy_to_point(local_state);
                                    robot_trajs[index]->copy_onto_back(local_state);
                                }
                            }
                        }
                        unblock();

                        robot_command.trajectory.points.clear();
                        unigripper_command.trajectory.points.clear();
                    //create message and send to robot

                        for(unsigned i=0;i<robot_trajs[index]->size()-1;i++)
                        {
                            trajectory_msgs::JointTrajectoryPoint point;
                            trajectory_msgs::JointTrajectoryPoint point_uni;
                            point.time_from_start = ros::Duration(duration);
                            point_uni.time_from_start = ros::Duration(duration);
                            control_t* control = robot_plans[index]->get_control_at(duration);
                        //add the state to the trajectory point
                            for( unsigned j = 0; j < 16; j++ )
                            {
                                point.positions.push_back((*robot_trajs[index])[i]->at(name_index_map[dof_names[j]]));
                                point.velocities.push_back(control->at(name_index_map[dof_names[j]]));
                                point.accelerations.push_back(0);
                            }
                            duration+=simulation::simulation_step;
                            robot_command.trajectory.points.push_back(point);

                            point_uni.positions.push_back((*robot_trajs[index])[i]->at(8));
                            point_uni.positions.push_back(control->at(8));
                            point_uni.accelerations.push_back(0);

                            unigripper_command.trajectory.points.push_back(point_uni);

                        }
                    }
                    else if(grasp_plan[index] && automaton_state!=MOVE_TO_HOME)
                    {
                        if (robot_plans[index]->at(0).duration >= (0.02 - PRX_ZERO_CHECK))
                        {
                            PRX_INFO_S("Plan: \n"<<robot_plans[index]->print(2));
                        //trigger a grasp
                            control_t* control = robot_plans[index]->get_control_at(0);

                        // PRX_PRINT("simulation_control: "<< simulation_control_space->print_point(simulation_control), PRX_TEXT_RED);
                            // PRX_PRINT("control: "<< control->at(16), PRX_TEXT_RED);

                            int gripper_control_index = -1;
                            ros::ServiceClient left_gripper_client;
                            prx_simulation::UnigripperVacuumOn uni_srv;
                            if (control->at(16)>=1)
                            {
                                PRX_INFO_S("Sending UniGripper command");
                                left_gripper_client = n.serviceClient<prx_simulation::UnigripperVacuumOn>("unigripper_vacuum");                            
                                gripper_control_index = 16;
                            }
                            else if (control->at(17)>=1)
                            {
                                PRX_INFO_S("Sending UniGripper command");
                                left_gripper_client = n.serviceClient<prx_simulation::UnigripperVacuumOn>("unigripper_vacuum2");                            
                                gripper_control_index = 17;
                            }
                            else if (control->at(18)>=1)
                            {
                                PRX_INFO_S("Sending UniGripper command");
                                left_gripper_client = n.serviceClient<prx_simulation::UnigripperVacuumOn>("unigripper_vacuum3");                            
                                gripper_control_index = 18;
                            }
                            else
                            {
                                PRX_WARN_S ("Attempted to control parallel gripper for some reason");
                                return 0.0;
                            }

                            if(control->at(gripper_control_index)>=1)
                            {
                                // PRX_INFO_S("Sending UniGripper command");
                                // ros::ServiceClient left_gripper_client = n.serviceClient<prx_simulation::UnigripperVacuumOn>("unigripper_vacuum");
                                // prx_simulation::UnigripperVacuumOn uni_srv;

                                if(control->at(gripper_control_index)==3)
                                {
                                    PRX_PRINT("@SIMULATION: Early grasp detected!",PRX_TEXT_RED);
                                    PRX_PRINT("@SIMULATION: Reverting control back to normal: 3->2 ",PRX_TEXT_GREEN);
                                // control->at(16)=2;
                                    uni_srv.request.TurnVacuumOn = true;
                                    early_grasp = true;
                                    actual_grasp = false;
                                }
                            else if(control->at(gripper_control_index) == 1)//Vacuum OFF
                            {
                                // PRX_PRINT("@SIMULATION:  SENDING UNIGRIPPER GRASPING COMMAND",PRX_TEXT_CYAN);
                                // simulator->get_state_space()->copy_to_point(simulation_state);
                                // PRX_PRINT("simulation_state 1: "<<simulator->get_state_space()->print_point(simulation_state), PRX_TEXT_GREEN);
                                // controller->send_unigripper_grasping_command(false);
                                // simulator->get_state_space()->copy_to_point(simulation_state);
                                // PRX_PRINT("simulation_state 2: "<<simulator->get_state_space()->print_point(simulation_state), PRX_TEXT_GREEN);

                                // simulator->propagate(simulation::simulation_step);
                                // simulator->get_state_space()->copy_to_point(simulation_state);
                                // PRX_PRINT("simulation_state 5: "<<simulator->get_state_space()->print_point(simulation_state), PRX_TEXT_GREEN);

                                uni_srv.request.TurnVacuumOn = false;
                            }
                            else if(control->at(gripper_control_index) == 2)//Vacuum ON
                            {
                                early_grasp = false;
                                actual_grasp = true;
                                
                                std::vector<manipulation::movable_body_plant_t*> objects;
                                dynamic_cast<manipulation_simulator_t*>(simulator)->get_movable_objects(objects);

                                int counter = 0;
                                int target_index=-1;
                                foreach(movable_body_plant_t* plant, objects)
                                {

                                    if(plant->get_object_type() == CurrentTarget)
                                    {
                                        target_index = counter;
                                        PRX_PRINT("target_index: "<<target_index, PRX_TEXT_GREEN);                             
                                    }
                                    counter++;
                                }   

                                robot_state->at(gripper_control_index) = 2;
                                manipulator->get_state_space()->copy_from_point(robot_state);
                                simulator->get_state_space()->copy_to_point(simulation_state);
                                simulation_state->at(simulator->get_state_space()->get_dimension()-2) = target_index;
                                simulator->push_state(simulation_state);

                                // PRX_PRINT("@SIMULATION:  SENDING UNIGRIPPER GRASPING COMMAND",PRX_TEXT_CYAN);
                                // simulator->get_state_space()->copy_to_point(simulation_state);
                                // PRX_PRINT("simulation_state 1: "<<simulator->get_state_space()->print_point(simulation_state), PRX_TEXT_GREEN);
                                // controller->send_unigripper_grasping_command(true);
                                // simulator->get_state_space()->copy_to_point(simulation_state);
                                // PRX_PRINT("simulation_state 2: "<<simulator->get_state_space()->print_point(simulation_state), PRX_TEXT_GREEN);
                                // // simulator->push_control();
                                // // simulator->get_state_space()->copy_to_point(simulation_state);
                                // // PRX_PRINT("simulation_state 3: "<<simulator->get_state_space()->print_point(simulation_state), PRX_TEXT_GREEN);
                                // // controller->set_completed();
                                // // controller->propagate(simulation::simulation_step);
                                // // simulator->get_state_space()->copy_to_point(simulation_state);
                                // // PRX_PRINT("simulation_state 4: "<<simulator->get_state_space()->print_point(simulation_state), PRX_TEXT_GREEN);
                                
                                // simulator->propagate(simulation::simulation_step);
                                // simulator->get_state_space()->copy_to_point(simulation_state);
                                // PRX_PRINT("simulation_state 3: "<<simulator->get_state_space()->print_point(simulation_state), PRX_TEXT_GREEN);

                                // int last_index = simulator->get_state_space()->get_dimension();
                                // PRX_PRINT("target_index: "<<target_index, PRX_TEXT_GREEN);
                                // PRX_PRINT("CurrentTarget: "<<CurrentTarget, PRX_TEXT_GREEN);
                                // simulation_state->at(last_index-2)=target_index;                 
                                // simulator->push_state(simulation_state);
                                // PRX_PRINT("after push temp state, simulation_state: "<<simulator->get_state_space()->print_point(simulation_state), PRX_TEXT_GREEN);

                                publish_state();
                                tf_broadcasting();

                                unigripper_control_on = control;                            
                                
                                uni_srv.request.TurnVacuumOn = true;

                                // output_control_space->copy_from_point(control);
                                // simulator->propagate(simulation::simulation_step);
                                // simulation_state_space->copy_to_point(simulation_state);

                                // PRX_PRINT("simulation_state: "<<simulation_state_space->print_point(simulation_state), PRX_TEXT_RED);
                            }
                        }
                        else
                        {
                            PRX_ERROR_S("Wrong UniGripper Control!");
                        }

                        left_gripper_client.waitForExistence();
                        left_gripper_client.call(uni_srv);
                        sleep(1);

                    }
                    // if(control->at(17)>=1)
                    // {
                    //     PRX_INFO_S("Sending gripper command");
                    //     ros::ServiceClient right_gripper_client = n.serviceClient<prx_simulation::gripper_change_srv>("/prx/reflex_grasp");
                    //     prx_simulation::gripper_change_srv srv;
                    //     srv.request.gripper_state = control->at(17);

                    //     right_gripper_client.waitForExistence();
                    //     right_gripper_client.call(srv);
                    // }

                }
                delete robot_trajs[index];
                delete robot_plans[index];
                child_state_space->free_point(local_state);
                return duration;
            }

            void apc_sim_application_t::send_robot_message(double duration)
            {
                PRX_INFO_S("Searching for the ac_server");
                bool found_ac_server = false;
                while( !found_ac_server )
                    found_ac_server = robot_ac->waitForServer(ros::Duration(0)) && unigripper_ac->waitForServer(ros::Duration(0));
                PRX_INFO_S("Found the ac_server!");

                // PRX_INFO_S("Plan: \n"<<robot_plans[index]->print(5));
                // PRX_INFO_S("Traj: \n"<<robot_trajs[index]->print(2));
                if(duration>0)
                {
                    robot_command.trajectory.header.stamp = ros::Time::now();
                    unigripper_command.trajectory.header.stamp = ros::Time::now();
                    PRX_INFO_S("Sending Robot command");
                    robot_ac->sendGoal(robot_command);
                    unigripper_ac->sendGoal(unigripper_command);
                }
                bool finished_before_timeout=false;
                double elapsed = 0.0;
                // PRX_WARN_S(ros::Duration(robot_plans[index]->length()+1.0));
                while(!finished_before_timeout && elapsed< duration + 4)
                {
                    elapsed+=.1;
                    finished_before_timeout = robot_ac->waitForResult(ros::Duration(.1)); //ros::Duration(robot_plans[index]->length()+

                    if(automaton_state == EXECUTE_REACHING && start_servoing())
                    {
                        controller->cancel_goal();
                        robot_ac->cancelGoal();
                        unigripper_ac->cancelGoal();
                        PRX_INFO_S("Done");
                    // has_goal = false;
                        controller->clear_robot_plan();
                        controller->set_completed();
                        pose_change = true;
                        break;

                    //LOGIC
                    }
                    //SURVOING LOGIC
                    // if(survoing)
                    // {
                    //     robot_ac->cancelGoal();
                    //     unigripper_ac->cancelGoal();
                    //     PRX_INFO_S("Done");
                    //     has_goal = false;
                    //     controller->clear_robot_plan();
                    //     controller->set_completed();
                    //     call_planning
                    //     controller->convert_and_copy_to_robot_plan(execute_command.plan);
                    //     controller->set_robot_plan();
                        
                    //     //ZPP
                    //     if(real_robot)
                    //     {
                    //         create_and_send_robot_command();
                    //     }


                    // }

                    ros::spinOnce();
                    tf_broadcasting();
                }
                if(!finished_before_timeout)
                {
                    PRX_INFO_S("Didn't finish before timeout");
                    robot_ac->cancelGoal();
                    unigripper_ac->cancelGoal();
                }
                else
                {
                    actionlib::SimpleClientGoalState state = robot_ac->getState();
                    ROS_INFO("Action finished: %s",state.toString().c_str());
                }
                ros::Rate rate(10);
                int counter =0;
            // has_goal = false;
                while(ros::ok() && counter++<11)
                {
                    rate.sleep();
                }
                controller->clear_robot_plan();
                controller->set_completed();
            // has_goal = true;
            }

                void apc_sim_application_t::clear_plan()
                {
                // has_goal = false;
                    controller->clear_robot_plan();
                    controller->set_completed();
                }

                void apc_sim_application_t::get_current_target_pose_callback(const geometry_msgs::PoseStamped::ConstPtr &msg)
                {
                // PRX_PRINT("1", PRX_TEXT_GREEN);
                    geometry_msgs::PoseStamped found = *msg;
                // PRX_PRINT("2", PRX_TEXT_GREEN);
                    current_target_obj_pose = {10,10,10,10,10,10,10};
                // PRX_PRINT("3", PRX_TEXT_GREEN);
                    if(found.pose.orientation.x == found.pose.orientation.x)
                    {
                        current_target_obj_pose[0] = found.pose.position.x;
                        current_target_obj_pose[1] = found.pose.position.y;
                        current_target_obj_pose[2] = found.pose.position.z;
                        current_target_obj_pose[3] = found.pose.orientation.x;
                        current_target_obj_pose[4] = found.pose.orientation.y;
                        current_target_obj_pose[5] = found.pose.orientation.z;
                        current_target_obj_pose[6] = found.pose.orientation.w;
                    }
                }

                bool apc_sim_application_t::start_servoing()
                {
                // //LOGIC
                // PRX_PRINT("Monitoring target obj pose", PRX_TEXT_GREEN);
                // PRX_PRINT("target_obj_pose: " << target_obj_pose, PRX_TEXT_GREEN);

                // // ros::NodeHandle sensing_node;
                // // std::vector<double> current_target_obj_pose;
                // // std::string topic = "/simtrack/"+CurrentTarget;
                // // PRX_PRINT("topic: "<<topic, PRX_TEXT_CYAN);
                // // get_current_target_obj_pose = sensing_node.subscribe<geometry_msgs::PoseStamped>(topic,100,&apc_sim_application_t::get_current_target_pose_callback, this);

                // bool sensing_success;
                // sensing_success = estimate_objects_pose();
                // if (sensing_success == true)
                // {
                //     foreach(movable_body_plant_t* plant, movable_bodies)
                //     {
                //         if(plant->get_object_type() == CurrentTarget)
                //         {
                //             std::vector<double> obj_pose_vector;
                //             config_t obj_pose;
                //             plant->get_configuration(obj_pose);
                //             obj_pose.copy_to_vector(obj_pose_vector);
                //             std::vector<double> pose_diff_vector;

                //             std::transform(obj_pose_vector.begin(), obj_pose_vector.end(), target_obj_pose.begin(),
                //                                  std::back_inserter(pose_diff_vector), [&](double l, double r)
                //             {
                //                 return std::abs(l - r);
                //             });
                //             double pose_diff_norm = 0;
                //             pose_diff_norm = l2_norm(pose_diff_vector);
                //             PRX_PRINT("pose_diff_norm: "<< pose_diff_norm, PRX_TEXT_GREEN);
                //             if (pose_diff_norm > POSE_CAHNGE_THRESHOLD)
                //             {
                //                 plant->update_collision_info();
                //                 objectMsg.name = CurrentTarget;
                //                 std::vector<double> pose_vec;
                //                 double quat[4];
                //                 obj_pose.get_position(pose_vec);
                //                 obj_pose.get_xyzw_orientation(quat);
                //                 pose_vec.push_back(quat[0]);
                //                 pose_vec.push_back(quat[1]);
                //                 pose_vec.push_back(quat[2]);
                //                 pose_vec.push_back(quat[3]);
                //                 objectMsg.elements = pose_vec;
                //                 object_publisher.publish(objectMsg);
                //                 plant = NULL;

                //                 target_obj_pose = obj_pose_vector;

                //                 publish_state();
                //                 return true;
                //             }      

                //         }
                //     }
                // }
                // PRX_PRINT("current_target_obj_pose: "<<current_target_obj_pose, PRX_TEXT_CYAN);

                // if (current_target_obj_pose[0] != 10)
                // {                    
                //     foreach(movable_body_plant_t* plant, movable_bodies)
                //     {
                //         if(plant->get_object_type() == CurrentTarget)
                //         {
                //             std::vector<double> pose_diff_vector;
                //             config_t obj_pose;
                // //             plant->get_configuration(obj_pose);
                // //             obj_pose.copy_to_vector(obj_pose_vector);

                //             std::transform(current_target_obj_pose.begin(), current_target_obj_pose.end(), target_obj_pose.begin(),
                //                                  std::back_inserter(pose_diff_vector), [&](double l, double r)
                //             {
                //                 return std::abs(l - r);
                //             });  

                //             double pose_diff_norm = 0;
                //             pose_diff_norm = l2_norm(pose_diff_vector);
                //             PRX_PRINT("pose_diff_norm: "<< pose_diff_norm, PRX_TEXT_GREEN);
                //             if (pose_diff_norm > POSE_CAHNGE_THRESHOLD)
                //                 {
                //                     plant->update_collision_info();
                //                     objectMsg.name = CurrentTarget;
                //                     std::vector<double> pose_vec;
                //                     double quat[4];
                //                     obj_pose.get_position(pose_vec);
                //                     obj_pose.get_xyzw_orientation(quat);
                //                     pose_vec.push_back(quat[0]);
                //                     pose_vec.push_back(quat[1]);
                //                     pose_vec.push_back(quat[2]);
                //                     pose_vec.push_back(quat[3]);
                //                     objectMsg.elements = pose_vec;
                //                     object_publisher.publish(objectMsg);
                //                     plant = NULL;

                //                     target_obj_pose = current_target_obj_pose;

                //                     publish_state();
                //                     return true;
                //                 }    
                //         }           
                //     }
                // }
                    return false;
                }


                std::string apc_sim_application_t::getStringFromEnum(automaton_states a_s)
                {
                  switch (a_s)
                  {
                      case START: return "START";
                      case SHELF_DETECTION: return "SHELF_DETECTION";
                      case TARGET_SELECTION: return "TARGET_SELECTION";
                      case MOVE_AND_SENSE: return "MOVE_AND_SENSE";
                      case MOVE_AND_SENSE_TWO: return "MOVE_AND_SENSE_TWO";
                      case MOVE_AND_SENSE_THREE: return "MOVE_AND_SENSE_THREE";
                      case POSE_ESTIMATION: return "POSE_ESTIMATION";
                      case GRASP_PLANNING: return "GRASP_PLANNING";
                      case BLOCKING_ITEM_SELECTION: return "BLOCKING_ITEM_SELECTION";
                      case EXECUTE_REACHING: return "EXECUTE_REACHING";
                      case EVALUATE_GRASP: return "EVALUATE_GRASP";
                      case RETRY_GRASP: return "RETRY_GRASP";
                      case ADJUST_EE: return "ADJUST_EE";
                      case EXECUTE_GRASP: return "EXECUTE_GRASP";
                      case DISENGAGE_EE: return "DISENGAGE_EE";
                      case PLAN_FOR_BLOCKING_ITEM: return "PLAN_FOR_BLOCKING_ITEM";
                      case PLAN_FOR_TARGET_ITEM: return "PLAN_FOR_TARGET_ITEM";
                      case EXECUTE_PLACING: return "EXECUTE_PLACING";
                      case PLAN_FOR_RETRACTION: return "PLAN_FOR_RETRACTION";
                      case STOP_ROBOT: return "STOP_ROBOT";
                      case TURN_OFF_SENSING: return "TURN_OFF_SENSING";
                      case MOVE_TO_HOME: return "MOVE_TO_HOME";
                      case EXECUTE_REGRASP: return "EXECUTE_REGRASP";
                      case LIFT: return "LIFT";
                      case EXECUTE_LIFT: return "EXECUTE_LIFT";
                      case END: return "END";
                      default: return "!!!Bad enum!!!";
                  }
              }
          }
      }
  }