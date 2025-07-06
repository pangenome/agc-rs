#include <string>
#include "raduls.h"
#include "defs.h"
namespace raduls
{
	namespace exceptions
	{
		InputNotAlignedException::InputNotAlignedException(uint64_t alignment) :std::logic_error("Input array is not properly aligned to " + std::to_string(alignment))
		{
		}

		TempNotAlignedException::TempNotAlignedException(uint64_t alignment) : std::logic_error("Temporary array is not properly aligned to " + std::to_string(alignment))
		{
		}

		RecSizeNotMultipleOf8Exception::RecSizeNotMultipleOf8Exception() : std::logic_error("Rec size must be a multiple of 8")
		{
		}


		KeySizeGreaterThanRecSizeException::KeySizeGreaterThanRecSizeException() : std::logic_error("Key size cannot be greater than rec size")
		{
		}

		UsupportedRecSizeException::UsupportedRecSizeException() : std::logic_error("This rec size is not supported. Try to extend MAX_REC_SIZE_IN_BYTES")
		{
		}

		UnsupportedHardware::UnsupportedHardware() : std::logic_error(std::string("raduls requires at least ") + MINIMAL_REQUIRED_INSTR_SET)
		{
		}

		UndetectedHardware::UndetectedHardware() : std::logic_error("Raduls was not able to detect CPU instruction set")
		{
		}		

		TooManyPhases::TooManyPhases() : std::logic_error("Number of phases cannot be greater than key size")
		{
		}		
	}
}
