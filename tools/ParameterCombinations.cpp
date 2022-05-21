
#include "parameterCombinations.h"
#include <sstream>


void ParameterCombinations::CartesianRecurse(std::vector<std::vector<int64_t>> &accum, std::vector<int64_t> stack,
	std::vector<std::vector<int64_t>> sequences,int64_t index)
{
	std::vector<int64_t> sequence = sequences[index];
	for (int64_t i : sequence)
	{
		stack.push_back(i);
		if (index == 0) {
			accum.push_back(stack);
		}
		else {
			CartesianRecurse(accum, stack, sequences, index - 1);
		}
		stack.pop_back();
	}
}

std::vector<std::vector<int64_t>> ParameterCombinations::CartesianProduct(std::vector<std::vector<int64_t>>& sequences)
{
	std::vector<std::vector<int64_t>> accum;
	std::vector<int64_t> stack;
	if (sequences.size() > 0) {
		CartesianRecurse(accum, stack, sequences, sequences.size() - 1);
	}
	return accum;
}

ParameterCombinations::ParameterCombinations(parameterCombinations_t& paramCombs, dontCares_t& dontCares, std::string& dontCareKey,
	parameterTypeMap_t& parameterTypeMap, printableParams_t& printableParameters)
	: parameterTypeMap_(parameterTypeMap)
	, printableParameters_(printableParameters)
{
	// Convert parameterCombinations_t to a vector of vector of ints
	std::vector<std::vector<int64_t>> sequences;
	for (auto& param : paramCombs) {
		std::vector<int64_t> seq;
		if (parameterTypeMap_["string"].count(param.first))
		{
			for (auto& val : param.second) {
				std::string* strAddress = &std::get<std::string>(val);
				seq.push_back(reinterpret_cast<int64_t&>(strAddress));
			}
		}
		else
		{
			for (auto& val : param.second) {
				seq.push_back(reinterpret_cast<int64_t&>(val));
			}
		}
		sequences.push_back(seq);
	}

	std::vector<std::vector<int64_t>> res = CartesianProduct(sequences);

	// Eliminate duplicates
	std::set<std::vector<int64_t>> resSet;
	for (auto& v : res) {
		resSet.insert(v);
	}

	// Create parameter combinations with the combinatorial generated above
	parameterCombinations_t newParamCombs;
	for (auto& set : resSet) {
		int i = 0;
		for (auto& param : paramCombs)
		{
			// Reverse the set and insert
			if (parameterTypeMap_["double"].count(param.first))
			{
				int64_t a = static_cast<int64_t>(set[set.size() - i - 1]);
				my_float b = reinterpret_cast<my_float&>(a);
				newParamCombs[param.first].push_back(b);
			}
			else if (parameterTypeMap_["string"].count(param.first))
			{
				int64_t a = static_cast<int64_t>(set[set.size() - i - 1]);
				std::string* b = reinterpret_cast<std::string*>(a);
				newParamCombs[param.first].push_back(*b);
			}
			else
			{
				auto a = static_cast<int>(set[set.size() - i - 1]);
				newParamCombs[param.first].push_back(a);
			}
			i++;
		}
	}
	
	// Remove repeated combinations taking into account don't care parameters
	ParameterInstanceSetCompare cmp(dontCares, dontCareKey, parameterTypeMap_);
	parameterInstanceSet_ = std::make_unique<parameterInstanceSet_t>(cmp);

	for (size_t paramIdx = 0; paramIdx < newParamCombs["buflen"].size(); paramIdx++)
	{
		// Select the parameters to use for this iteration of the test
		parameterInstanceMap_t paramInstance;
		for (auto& [paramName, paramValues] : newParamCombs) {
			paramInstance[paramName] = paramValues[paramIdx];
		}
		parameterInstanceSet_->insert(paramInstance);
	}
}

std::string ParameterCombinations::constructVariationName(const parameterInstanceMap_t& paramInstance)
{
	std::string variationName;
	for (auto [paramName, paramValue] : paramInstance)
	{
		if (!printableParameters_.count(paramName))
		{
			continue;
		}
		if (paramName == "inputFile" || paramName == "signal")
		{
			variationName += std::get<std::string>(paramValue) + "_";
		}
		else if (paramName == "algo")
		{
			variationName += paramName + "_" + std::get<std::string>(paramValue) + "_";
		} 
		else if (parameterTypeMap_["double"].count(paramName))
		{
			std::stringstream ss;
			ss << std::get<my_float>(paramValue) << std::scientific;
			variationName += paramName + "_" + ss.str() + "_";
		}
		else
		{
			variationName += paramName + "_" + std::to_string(std::get<int>(paramValue)) + "_";
		}
	}

	// Remove trailing "_"
	size_t lastIndex = variationName.find_last_of("_");
	variationName = variationName.substr(0, lastIndex);

	return variationName;

}


const parameterInstanceSet_t* ParameterCombinations::getParameterInstanceSet()
{
	return parameterInstanceSet_.get();
}

