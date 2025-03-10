/*
main.cpp
Created on: Dec 6, 2024
    Author: Alessio/Shawn
 */

#include <random>
#include <assert.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <mutex>
#include <taskflow.hpp>
#include <timer.hpp>

#define DT 				1e-3  	// time step
#define MAX_B			10		// Max value of b values to use
#define B_VAL_INCREMENT 1		// b value increment
#define M				2e5		// M value defined in assignment
#define THREADS_NUM		512		// Max number of threads for parallel calc
#define WINDOWS_NUM		1000	// Number of windows to divide IC vec into (jobs)

void printToFile(const std::vector<std::vector<double>> extinctionTimes,
		const std::vector<double> b_vals,
		const std::vector<double> timesteps){

	// Open a file for writing
	std::ofstream filex("xdata");
	std::ofstream filey("ydata");

	// Print to file
	// Print time steps to filex
	for (size_t i = 0; i < timesteps.size(); ++i) {
		filex << timesteps[i];
		if (i != timesteps.size() - 1) filex << " ";
	}
	filex << "\n";

	// Append b value for associated row
	for (size_t i = 0; i < b_vals.size(); ++i) {
		filey << b_vals[i] << " ";
		// Print vectors of extinction times for various b values
		for (size_t j = 0; j < extinctionTimes[i].size(); ++j) {
			filey << extinctionTimes[i][j];
			if (j != extinctionTimes[i].size() - 1) filey << " ";
		}
		filey << "\n";
	}

	// Close file stream
	filex.close();
	filey.close();
}



// Generate M initial conditions for equation (1) in assignment
// iid sampled from a Gamma distribution with alpha=2, beta=1/b
// C++ ref: https://en.cppreference.com/w/cpp/numeric/random/gamma_distribution
std::vector<double> generateICSamples(std::vector<double>& ICsamples,
		const int samplesNo, const double b) {
	assert(b > 0);

	std::random_device rd;  	// Seed generator
	std::mt19937 gen(rd());  	// Random number generator
	const double shape_parameter = 2.0; 	// 'alpha' value as per assignment
	const double scale_parameter = 1/b; 	// 'beta' value as per assignment
	std::gamma_distribution<> dGamma(shape_parameter, scale_parameter);

	for (int i = 1; i<=samplesNo; ++i) {
		ICsamples.emplace_back(dGamma(gen));
	}

	return ICsamples;
}

// Cumulative probability of extinction - P(t>s)
// Calculate P(t>s), according to equation (2) in assignment
std::vector<double> extinctionTimesSerial(std::vector<double>& times, double b) {

	// Initial conditions vector X0
	std::vector<double> ICsamples;
	generateICSamples(ICsamples, M, b);

	/* Calculate the extinction time for each IC
       using the  Euler-Maruyama scheme */
	std::vector<double> extinctionTimes;
	std::random_device rd; // Seed generator
	std::mt19937 gen(rd()); // Random number generator
	std::normal_distribution<> dStandard(0.0, 1.0); // Standard normal pdf
	// For each IC (m) in ICsamples (M)
	for (int m = 0; m < M; m++) {
		// start stepping using the
		double Xn = ICsamples[m] ;
		for (int n = 0; ;n++) {
			Xn = Xn + (-b)*DT + sqrt(DT)*dStandard(gen);
			if (Xn <= 0) {
				// record the extinction time Tm=n*dt
				// break out
				extinctionTimes.emplace_back(n*DT);
				break;
			}
		}
	}

	return extinctionTimes;
}
// Cumulative probability of extinction - P(t>s)
// Calculate P(t>s), according to equation (2) in assignment
std::vector<double> extinctionTimesParallel(
		std::vector<double>& times, double b, uint16_t numThreads) {

	// Initial conditions vector X0
	std::vector<double> ICsamples;
	generateICSamples(ICsamples, M, b);

	// Taskflow initialisation
	tf::Executor executor(numThreads);
	tf::Taskflow taskflow;

	// Vector of vectors of extinction times
	// Each thread is assigned a section of extinction
	// time to calculate. Each "window" is stored as
	// a separate vector in this vector which is then
	// combined at the end of this function
	std::vector<std::vector<double>> mextinction;

	// Initialise each thread
	for (int i = 0; i <= WINDOWS_NUM; ++i) {
		// Stores each threads extinction times
		// Assigned to vector of windows declared above
		std::vector<double> windowExtinction;
		mextinction.push_back(windowExtinction);

		taskflow.emplace(
				[&mextinction, ICsamples, i, b] () {
			/* Calculate the extinction time for each IC
						using the  Euler-Maruyama scheme */
			std::random_device rd; // Seed generator
			std::mt19937 gen(rd()); // Random number generator
			std::normal_distribution<> dStandard(0.0, 1.0); // Standard normal pdf
			// For each IC (m) in ICsamples (M)
			for (int m = i*M/WINDOWS_NUM; m < M/WINDOWS_NUM*(i+1); m++) {
			// start stepping using the EM scheme
				double Xn = ICsamples[m] ;
				for (int n = 0; ;n++) {
					Xn = Xn + (-b)*DT + sqrt(DT)*dStandard(gen);
					if (Xn <= 0) {
						// record the extinction time Tm=n*dt
						// break out
						{
							mextinction[i].emplace_back(n*DT);
						}
						break;
					}
				}
			}
		}
		);	// End of thread initialisation

	}

	// Start threads. Wait for all threads to finish
	executor.run(taskflow);
	executor.wait_for_all();

	// Final vector of extinction times
	// We combine the vector of windows into a single vector
	std::vector<double> r_extinctionTimes;
	for (size_t i = 0; i < mextinction.size(); ++i) {
		r_extinctionTimes.insert(r_extinctionTimes.end(),
				mextinction[i].begin(), mextinction[i].end());
	}
	return r_extinctionTimes;

}

// Calculate extinction times
// Uses serial or parallel implementation based on number of threads provided
std::vector<double> probabilityExtinctionTimes(std::vector<double>& times,
		double b, uint16_t numThreads) {

	// Generates vector of extinction times
	std::vector<double> r_extinctionTimes;
	if(numThreads > 1){
		r_extinctionTimes = extinctionTimesParallel(times, b, numThreads);
	}else{
		r_extinctionTimes = extinctionTimesSerial(times, b);
	}

	// Calculation of probability
	// for each time in times vector
	std::vector<double> probability;
	for (size_t n = 0; n < times.size(); n++) {
		double sum = 0;
		for (int m = 0; m < M; m++) {
			sum += (r_extinctionTimes[m] > times[n]);
		}
		probability.emplace_back(sum / M);
	}
	return probability;
}


int main() {

	using namespace std::chrono;
	sf::Timer timer;

	// Create a vector of times
	std::vector<double> timesteps;
	for(double i = 0; i < 1; i+=DT)
		timesteps.emplace_back(i);

	// Create a vector of b values (include 1)
	std::vector<double> b;
	for(int i = 1; i < MAX_B; i+=B_VAL_INCREMENT){
		b.emplace_back(i);
	}

	std::vector<std::vector<double>> extinctionTimes;

	// Calculate for different number of threads
	std::cout << "Threads | Time Taken: " << std::endl;
	for(uint16_t threads = 1; threads <= THREADS_NUM; threads+=4){
		extinctionTimes.clear();
		timer.start(std::to_string(threads));

		// Calculate extinction times in parallel
		for(size_t n = 0; n < b.size(); n++)
			extinctionTimes.emplace_back(
				probabilityExtinctionTimes(timesteps, b[n],	512)
			);

		timer.stop();
	}

	printToFile(extinctionTimes, b, timesteps);

	return 1;
}
