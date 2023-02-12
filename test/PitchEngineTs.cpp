
#include "PitchEngineTs.h"
#include "wavio.h"
#include "StrechEngine.h"
#include "CQPVEngine.h"
#include "PVDREngine.h"
#include "NNPVEngineTrainer.h"
#include "VariableDumper.h"
#include "logger.h"
//#include "TimerContainer.h"
#include "parameterTemplates.h"
#include "maximilian.h"
#include "TaskScheduler.h"
#ifdef WIN32
#include <filesystem>
#else
#include <experimental/filesystem>
#endif
#include <thread>
#include <queue>
#ifdef USE_MULTITHREADING
#define NUM_THREADS 8
#else
#define NUM_THREADS 1
#endif

#define INGAIN     1
#define OUTGAIN    1

using task_t = std::tuple<std::string, std::string, std::string, parameterInstanceMap_t>;

//TimerContainer* TimerContainer::instance = 0;
TaskScheduler<task_t>* TaskScheduler<task_t>::instance = 0;

int PitchEngineTs()
{
	PRINT_LOG("Starting test");
	//ParameterCombinator paramSet = generateInputFileCombinations();
	ParameterCombinator paramSet = sineSweepCombinations();

	auto t1 = std::chrono::high_resolution_clock::now();
	runTest(paramSet);
	auto t2 = std::chrono::high_resolution_clock::now();
	auto exTime = std::chrono::duration_cast<std::chrono::seconds>(t2 - t1);
	PRINT_LOG("Test took ", exTime.count(), "s");

	std::vector<std::string> failedTests = getFailedTests(paramSet, TEST_AUDIO_DIR, OUTPUT_AUDIO_DIR);

	for (auto& failedTest : failedTests)
	{
		std::cout << "Test: " << failedTest << " failed." << std::endl;
	}

	return failedTests.size() != 0;

}

void runTest(ParameterCombinator& paramSet)
{
	std::queue<task_t> tasks;
	TaskScheduler<task_t>* taskScheduler = TaskScheduler<task_t>::getInstance(NUM_THREADS);

	for (auto& paramInstance : *paramSet.getParameterInstanceSet())
	{
		task_t task{};

		std::string& inputFilePath  = std::get<0>(task);
		std::string& outputFilePath = std::get<1>(task);
		std::string& variationName  = std::get<2>(task);

		parameterInstanceMap_t& paramInstanceTask = std::get<3>(task);
		paramInstanceTask = paramInstance;

		variationName = ParameterCombinator::generateCombinationName(paramInstance);

		// Reading from file
		if (paramInstance.count("inputFile"))
		{
			std::string inputFileName = getVal<const char*>(paramInstance, "inputFile");
			removeFileExtension(inputFileName);
			inputFilePath  = INPUT_AUDIO_DIR + inputFileName + ".wav";

			outputFilePath = OUTPUT_AUDIO_DIR + inputFileName + "/";
			std::filesystem::create_directory(outputFilePath);
			outputFilePath += variationName + ".wav";
		}
		// Synthezising signal
		else if (paramInstance.count("signal"))
		{
			if (!strcmp(getVal<const char*>(paramInstance, "data"), "features"))
			{
				outputFilePath = INPUT_FOR_TRAINING_AUDIO_DIR + variationName + ".wav";
			}
			else if (!strcmp(getVal<const char*>(paramInstance, "data"), "labels"))
			{
				outputFilePath = TRAINING_AUDIO_DIR + variationName + ".wav";
			}
		}
		taskScheduler->addTask(std::move(task));
	}
	taskScheduler->run();
}

void runPitchEngine(std::string inputFilePath, std::string outputFilePath, std::string variationName, parameterInstanceMap_t paramInstance)
{
	PRINT_LOG("Test ", variationName);
	SET_DUMPERS_PATH(DEBUG_DIR + variationName + "/");
	Timer runPitchEngineTimer("runPitchEngineTimer", timeUnit::MILISECONDS);

	int audio_ptr     = 0;                       // Wav file sample pointer
	int sampleRate    = 44100;
	int bitsPerSample = 16;
	std::string debugFolder;

	std::vector<float> in_audio;

	// Generate training data
	if (paramInstance.count("data"))
	{
		generateSignal(in_audio, paramInstance, debugFolder, sampleRate);
		if (!strcmp(getVal<const char*>(paramInstance, "data"), "labels"))
		{
			writeWav(in_audio, outputFilePath, sampleRate, bitsPerSample);
			return;
		}
		// Generated audio for training the NN
		else if (!strcmp(getVal<const char*>(paramInstance, "data"), "features"))
		{
			writeWav(in_audio, outputFilePath, sampleRate, bitsPerSample);
		}
	}
	// Test algorithm 
	else if (paramInstance.count("inputFile"))
	{
		debugFolder = getVal<const char*>(paramInstance, "inputFile");
		in_audio = readWav(inputFilePath);
	}

	int buflen  = getVal<int>(paramInstance, "buflen");
	int steps   = getVal<int>(paramInstance, "steps");
	int hopA    = getVal<int>(paramInstance, "hopA");
	int numSamp = getVal<int>(paramInstance, "numSamp");

	my_float shift    = POW(2, (steps/12));
	int hopS          = static_cast<int>(ROUND(hopA * shift));
	int numFrames     = static_cast<int>(buflen / hopA);

	std::vector<float> out_audio(in_audio.size(), 0.0);

	std::unique_ptr<PitchEngine> pe;
	std::string	algo = getVal<const char*>(paramInstance, "algo");

	if (algo == "pv")
	{
		pe = std::make_unique<PVEngine>(steps, buflen, hopA);
	}
	else if (algo == "trainNN")
	{
		pe = std::make_unique<NNPVEngineTrainer>(steps, buflen, hopA);
	}
	else if (algo == "pvdr")
	{
		my_float magTol = getVal<my_float>(paramInstance, "magTol");
		pe = std::make_unique<PVDREngine>(steps, buflen, hopA, magTol);
	}
	else if (algo == "cqpv")
	{
		my_float magTol = getVal<my_float>(paramInstance, "magTol");
		pe = std::make_unique<CQPVEngine>(steps, buflen, hopA, sampleRate, magTol);
	}
	else if (algo == "se")
	{
		pe = std::make_unique<StrechEngine>(steps, buflen, hopA);
	}

	while(audio_ptr < (in_audio.size() - buflen))
	{
		for (int k = 0; k < buflen; k++)
		{
			pe->inbuffer_[k] = in_audio[audio_ptr + k] * INGAIN;
		}

		pe->process();

		// Algorithm is being tested
		if (paramInstance.count("inputFile"))
		{
			for (int k = 0; k < buflen; k++)
			{
				out_audio[audio_ptr + k] = pe->outbuffer_[k] * OUTGAIN;

				// Avoid uint16_t overflow and clip the signal instead.
				if (std::abs(out_audio[audio_ptr + k]) > 1)
				{
					out_audio[audio_ptr + k] = (out_audio[audio_ptr + k] < 0) ? -1.0 : 1.0;
				}
			}
		}
		audio_ptr += buflen;
	}

	// Algorithm is being tested
	if (paramInstance.count("inputFile"))
	{
		writeWav(out_audio, outputFilePath, sampleRate, bitsPerSample);
	}

	// DUMP_TIMINGS("timings.csv");
	DESTROY_DUMPERS();
}

std::vector<std::string> getFailedTests(ParameterCombinator& paramSet, std::string testFileDir, std::string outputFileDir)
{

	std::vector<std::string> failedTests;

	for (auto& paramInstance : *paramSet.getParameterInstanceSet())
	{

		if (paramInstance.count("signal"))
		{
			return failedTests;
		}

		std::string testFilePath{ testFileDir };
		std::string outputFilePath{ outputFileDir };

		std::string	inputFileName = getVal<const char*>(paramInstance, "inputFile");

		removeFileExtension(inputFileName);

		std::string variationName = paramSet.generateCombinationName(paramInstance);

		outputFilePath += inputFileName;
		testFilePath   += inputFileName;
		outputFilePath += "/";
		testFilePath   += "/";
		outputFilePath += variationName + ".wav";
		testFilePath   += variationName + ".wav";

		if (!std::filesystem::exists(outputFilePath) || !std::filesystem::exists(testFilePath))
		{
			continue;
		}

		std::vector<float> output_audio = readWav(outputFilePath);
		std::vector<float> test_audio = readWav(testFilePath);

		if (output_audio.size() != test_audio.size())
		{
			failedTests.push_back(variationName);
			continue;
		}
		bool failed = false;
		for (int i = 0; i < output_audio.size() && !failed; i++)
		{
			if (output_audio[i] != test_audio[i])
			{
				failedTests.push_back(variationName);
				failed = true;
			}
		}
	}
	return failedTests;
}

void removeFileExtension(std::string& str)
{
	size_t lastIndex = str.find_last_of(".");
	if (lastIndex != std::string::npos)
	{
		str = str.substr(0, lastIndex);
	}
}

void generateSignal(std::vector<float>& signal, parameterInstanceMap_t& paramInstance, std::string& debugFolder, int sampleRate)
{
	double frequency = getVal<double>(paramInstance, "freq");
	int numSamp   = getVal<int>(paramInstance, "numSamp");
	int amplitude = 1;
	std::string signalType  = getVal<const char*>(paramInstance, "signal");
	maxiOsc functionGen;

	debugFolder = signalType + "_" + std::to_string(frequency);

	signal.resize(numSamp);

	if (signalType == "sine")
	{
		for (int i = 0; i < numSamp; i++)
		{
			signal[i] = static_cast<float>(functionGen.sinewave(frequency));
		}
	}
}

