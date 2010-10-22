/**
 * Run parallel junction tree gibbs sampling on a factorized model
 */

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>


#include <graphlab.hpp>


#include "image.hpp"
#include "data_structures.hpp"
#include "sequential_jt_gibbs.hpp"



#include "jt_worker.hpp"





#include <graphlab/macros_def.hpp>




std::string results_fn = "experiment_results.tsv";


size_t get_next_experiment_id(const std::string& experiment_file) {
  std::ifstream fin(experiment_file.c_str());
  size_t lines = 0;
  std::string line;
  while(getline(fin, line)) lines++;
  fin.close();
  return lines;
}





int main(int argc, char** argv) {
  // set the global logger
  // global_logger().set_log_level(LOG_WARNING);
  // global_logger().set_log_to_console(true);


  std::cout << "This program runs junction tree blocked MCMC "
            << "inference on large factorized models."
            << std::endl;


  std::srand ( graphlab::timer::usec_of_day() );
  graphlab::random::seed();


  std::string model_filename = "";

  size_t treesize = 1000;
  size_t treeheight = 0;
  bool priorities = false;
  std::vector<float> runtimes(1,10);
  size_t treewidth = 3;
  size_t factorsize = 0;
  size_t subthreads = 1; 


  // Command line parsing
  graphlab::command_line_options clopts("Parallel Junction Tree MCMC");
  clopts.attach_option("model", 
                       &model_filename, model_filename,
                       "Alchemy formatted model file");
  clopts.add_positional("model");

  clopts.attach_option("runtime", 
                       &runtimes, runtimes,
                       "total runtime in seconds");

  clopts.attach_option("treesize", 
                       &treesize, treesize,
                       "The number of variables in a junction tree");

  clopts.attach_option("treeheight", 
                       &treeheight, treeheight,
                       "The height of the tree.");



  clopts.attach_option("treewidth", 
                       &treewidth, treewidth,
                       "The maximum treewidth");

  clopts.attach_option("factorsize", 
                       &factorsize, factorsize,
                       "The maximum factorsize");

  clopts.attach_option("subthreads", 
                       &subthreads, subthreads,
                       "The number of threads to use inside each tree");


  clopts.attach_option("priorities",
                       &priorities, priorities,
                       "Use priorities?");



  clopts.scheduler_type = "fifo";
  clopts.scope_type = "edge";
  if( !clopts.parse(argc, argv) ) { 
    std::cout << "Error parsing command line arguments!"
              << std::endl;
    return EXIT_FAILURE;
  }


  std::cout << "Load alchemy file." << std::endl;
  factorized_model factor_graph;
  factor_graph.load_alchemy(model_filename);

  std::cout << "Building graphlab MRF." << std::endl;
  mrf::graph_type mrf_graph;
  construct_mrf(factor_graph, mrf_graph);
  

  float run_so_far = 0;
  foreach(float runtime, runtimes) {
    
    // Get the experiment id
    size_t experiment_id = get_next_experiment_id(results_fn);

    std::cout << "Settings: ======================" << std::endl
              << "Experiment:    " << experiment_id << std::endl
              << "Model:         " << model_filename << std::endl
              << "runtime:       " << runtime << std::endl
              << "treesize:      " << treesize << std::endl
              << "treewidth:     " << treewidth << std::endl
              << "treeheight:    " << treeheight << std::endl
              << "factorsize:    " << factorsize << std::endl
              << "subthreads:    " << subthreads << std::endl
              << "priorities:    " << priorities << std::endl;
   
    // run the fully parallel sampler
    float remaining_time = runtime - run_so_far;
    if(remaining_time <= 0) remaining_time = 0;

    graphlab::timer timer;
    timer.start();
    parallel_sample(factor_graph, mrf_graph, 
                    clopts.ncpus,
                    remaining_time,
                    treesize,
                    treewidth,
                    factorsize,
                    treeheight,
                    subthreads,
                    priorities);
    double actual_runtime = timer.current_time();
    std::cout << "Local Runtime: " << actual_runtime << std::endl;
    
    run_so_far += actual_runtime;
    std::cout << "Total Runtime: " << run_so_far << std::endl;
    
    std::cout << "Computing unnormalized log-likelihood" << std::endl;
    double loglik = unnormalized_loglikelihood(mrf_graph,
                                               factor_graph.factors());
    
    std::cout << "LogLikelihood: " << loglik << std::endl;
    std::cout << "Saving final prediction" << std::endl;
    
    
    std::cout << "Computing update distribution:" << std::endl;
    mrf::save_beliefs(mrf_graph,  
                      make_filename("beliefs",".tsv", experiment_id).c_str());
    
    
    std::cout << "Computing update counts:" << std::endl;
    size_t total_updates = 0;
    for(vertex_id_t vid = 0; vid < mrf_graph.num_vertices(); ++vid) {
      mrf::vertex_data& vdata = mrf_graph.vertex_data(vid);
      total_updates += vdata.updates;
    }



    std::ofstream fout(results_fn.c_str(),  std::ios::app);
    fout.precision(16);
    fout << experiment_id << '\t'
         << clopts.ncpus << '\t'
         << run_so_far << '\t'
         << runtime << '\t'
         << treesize << '\t'
         << treewidth << '\t'
         << factorsize << '\t'
         << treeheight << '\t'
         << subthreads << '\t'
         << priorities << '\t'
         << actual_runtime << '\t'
         << total_updates << '\t'
         << loglik << std::endl;
    fout.close();


    
    // Plot the final answer
    size_t rows = std::sqrt(mrf_graph.num_vertices());
    std::cout << "Rows: " << rows << std::endl;
    image img(rows, rows);
    std::vector<double> values(1);
    for(vertex_id_t vid = 0; vid < mrf_graph.num_vertices(); ++vid) {
      mrf::vertex_data& vdata = mrf_graph.vertex_data(vid);
      vdata.belief.normalize();
      vdata.belief.expectation(values);
      img.pixel(vid) = values[0];
    }
    img.pixel(0) = 0;
    img.pixel(1) = mrf_graph.vertex_data(0).variable.arity-1;
    img.save(make_filename("pred", ".pgm", experiment_id).c_str());
    
    for(vertex_id_t vid = 0; vid < mrf_graph.num_vertices(); ++vid) {   
      img.pixel(vid) = mrf_graph.vertex_data(vid).updates;
    }
    img.save(make_filename("updates", ".pgm", experiment_id).c_str());
    
    for(vertex_id_t vid = 0; vid < mrf_graph.num_vertices(); ++vid) {   
      img.pixel(vid) = mrf_graph.vertex_data(vid).updates == 0;
    }
    img.save(make_filename("unsampled", ".pgm", experiment_id).c_str());
  
    for(vertex_id_t vid = 0; vid < mrf_graph.num_vertices(); ++vid) {   
      img.pixel(vid) = mrf_graph.vertex_data(vid).asg.asg_at(0);
    }
    img.save(make_filename("final_sample", ".pgm", experiment_id).c_str());

    for(vertex_id_t vid = 0; vid < mrf_graph.num_vertices(); ++vid) {   
      img.pixel(vid) = mrf_graph.vertex_data(vid).height;
    }
    img.save(make_filename("heights", ".pgm", experiment_id).c_str());


  } // end of for loop over runtimes

  

  




  return EXIT_SUCCESS;
} // end of main













#include <graphlab/macros_undef.hpp>


