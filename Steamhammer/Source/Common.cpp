#include "Common.h"

int GetIntFromString(const std::string & s)
{
	std::stringstream ss(s);
	int a = 0;
	ss >> a;
	return a;
}

// For example, "Zerg_Zergling" -> "Zergling"
std::string TrimRaceName(const std::string & s)
{
	if (s.substr(0, 5) == "Zerg_")
	{
		return s.substr(5, std::string::npos);
	}
	if (s.substr(0, 8) == "Protoss_")
	{
		return s.substr(8, std::string::npos);
	}
	if (s.substr(0, 7) == "Terran_")
	{
		return s.substr(7, std::string::npos);
	}

	// There is no race prefix. Return it unchanged.
	return s;
}
