#pragma once

#include "defs.h"
#include <algorithm>

//#define USE_BLOCK_QSORT

namespace raduls
{
	namespace NAMESPACE_NAME
	{
		namespace small_sort
		{
#ifdef USE_BLOCK_QSORT
			template<typename iter, typename Compare>
			inline void sort_pair(iter i1, iter i2, Compare less) {
				typedef typename std::iterator_traits<iter>::value_type T;
				bool smaller = less(*i2, *i1);
				T temp = std::move(smaller ? *i1 : temp);
				*i1 = std::move(smaller ? *i2 : *i1);
				*i2 = std::move(smaller ? temp : *i2);
			}

			template<typename iter, typename Compare>
			inline iter median_of_3(iter i1, iter i2, iter i3, Compare less) {
				sort_pair(i1, i2, less);
				sort_pair(i2, i3, less);
				sort_pair(i1, i2, less);
				return i2;
			}

			const int BLOCKSIZE = 32;

			template<typename iter, typename Compare>
			inline iter hoare_block_partition_simple(iter begin, iter end, iter pivot_position, Compare less) {
				typedef typename std::iterator_traits<iter>::difference_type index;
				index indexL[BLOCKSIZE], indexR[BLOCKSIZE];

				iter last = end - 1;
				std::iter_swap(pivot_position, last);
				//	const typename std::iterator_traits<iter>::value_type &pivot = *last;
				const typename std::iterator_traits<iter>::value_type pivot = *last;		// SD
				pivot_position = last;
				last--;

				int num_left = 0;
				int num_right = 0;
				int start_left = 0;
				int start_right = 0;
				int num;
				// main loop
				while (last - begin + 1 > 2 * BLOCKSIZE)
				{
					// Compare and store in buffers
					if (num_left == 0) {
						start_left = 0;
						index iL = 0;
						for (index j = 0; j < BLOCKSIZE;) {
							bool b0 = (!(less(begin[j], pivot)));		j++;
							bool b1 = (!(less(begin[j], pivot)));		j++;
							bool b2 = (!(less(begin[j], pivot)));		j++;
							bool b3 = (!(less(begin[j], pivot)));		j++;
							bool b4 = (!(less(begin[j], pivot)));		j++;
							bool b5 = (!(less(begin[j], pivot)));		j++;
							bool b6 = (!(less(begin[j], pivot)));		j++;
							bool b7 = (!(less(begin[j], pivot)));		j++;
							indexL[num_left] = iL++;	num_left += b0;
							indexL[num_left] = iL++;	num_left += b1;
							indexL[num_left] = iL++;	num_left += b2;
							indexL[num_left] = iL++;	num_left += b3;
							indexL[num_left] = iL++;	num_left += b4;
							indexL[num_left] = iL++;	num_left += b5;
							indexL[num_left] = iL++;	num_left += b6;
							indexL[num_left] = iL++;	num_left += b7;
						}
					}
					if (num_right == 0) {
						start_right = 0;
						index iR = 0;
						for (index j = 0; j < BLOCKSIZE;) {
							bool b0 = !(less(pivot, *(last - j)));			j++;
							bool b1 = !(less(pivot, *(last - j)));			j++;
							bool b2 = !(less(pivot, *(last - j)));			j++;
							bool b3 = !(less(pivot, *(last - j)));			j++;
							bool b4 = !(less(pivot, *(last - j)));			j++;
							bool b5 = !(less(pivot, *(last - j)));			j++;
							bool b6 = !(less(pivot, *(last - j)));			j++;
							bool b7 = !(less(pivot, *(last - j)));			j++;
							indexR[num_right] = iR++;	num_right += b0;
							indexR[num_right] = iR++;	num_right += b1;
							indexR[num_right] = iR++;	num_right += b2;
							indexR[num_right] = iR++;	num_right += b3;
							indexR[num_right] = iR++;	num_right += b4;
							indexR[num_right] = iR++;	num_right += b5;
							indexR[num_right] = iR++;	num_right += b6;
							indexR[num_right] = iR++;	num_right += b7;
						}
					}

					// rearrange elements
					num = (std::min)(num_left, num_right);
					for (int j = 0; j < num; j++)
						std::iter_swap(begin + indexL[start_left + j], last - indexR[start_right + j]);

					num_left -= num;
					num_right -= num;
					start_left += num;
					start_right += num;
					begin += (num_left == 0) ? BLOCKSIZE : 0;
					last -= (num_right == 0) ? BLOCKSIZE : 0;
				} // end main loop

				  // Compare and store in buffers final iteration
				index shiftR = 0, shiftL = 0;
				if (num_right == 0 && num_left == 0) { // for small arrays or in the unlikely case that both buffers are empty
					shiftL = ((last - begin) + 1) / 2;
					shiftR = (last - begin) + 1 - shiftL;
					start_left = 0; start_right = 0;
					for (index j = 0; j < shiftL; j++) {
						indexL[num_left] = j;
						num_left += (!less(begin[j], pivot));
						indexR[num_right] = j;
						num_right += !less(pivot, *(last - j));
					}
					if (shiftL < shiftR)
					{
						indexR[num_right] = shiftR - 1;
						num_right += !less(pivot, *(last - shiftR + 1));
					}
				}
				else if (num_right != 0) {
					shiftL = (last - begin) - BLOCKSIZE + 1;
					shiftR = BLOCKSIZE;
					start_left = 0;
					for (index j = 0; j < shiftL; j++) {
						indexL[num_left] = j;
						num_left += (!less(begin[j], pivot));
					}
				}
				else {
					shiftL = BLOCKSIZE;
					shiftR = (last - begin) - BLOCKSIZE + 1;
					start_right = 0;
					for (index j = 0; j < shiftR; j++) {
						indexR[num_right] = j;
						num_right += !(less(pivot, *(last - j)));
					}
				}

				// rearrange final iteration
				num = (std::min)(num_left, num_right);
				for (int j = 0; j < num; j++)
					std::iter_swap(begin + indexL[start_left + j], last - indexR[start_right + j]);

				num_left -= num;
				num_right -= num;
				start_left += num;
				start_right += num;
				begin += (num_left == 0) ? shiftL : 0;
				last -= (num_right == 0) ? shiftR : 0;
				// end final iteration

				// rearrange elements remaining in buffer
				if (num_left != 0)
				{
					int lowerI = start_left + num_left - 1;
					index upper = last - begin;
					// search first element to be swapped
					while (lowerI >= start_left && indexL[lowerI] == upper) {
						upper--; lowerI--;
					}
					while (lowerI >= start_left)
						std::iter_swap(begin + upper--, begin + indexL[lowerI--]);

					std::iter_swap(pivot_position, begin + upper + 1); // fetch the pivot
					return begin + upper + 1;
				}
				else if (num_right != 0) {
					int lowerI = start_right + num_right - 1;
					index upper = last - begin;
					// search first element to be swapped
					while (lowerI >= start_right && indexR[lowerI] == upper) {
						upper--; lowerI--;
					}

					while (lowerI >= start_right)
						std::iter_swap(last - upper--, last - indexR[lowerI--]);

					std::iter_swap(pivot_position, last - upper); // fetch the pivot
					return last - upper;
				}
				else { //no remaining elements
					std::iter_swap(pivot_position, begin); // fetch the pivot
					return begin;
				}
			}

			template<typename iter, typename Compare>
			struct Hoare_block_partition_simple {
				static inline iter partition(iter begin, iter end, Compare less) {
					// choose pivot
					//		iter mid = median_of_3(begin, begin + (end - begin) / 2, end, less);	
					iter mid = median_of_3(begin, begin + (end - begin) / 2, end - 1, less);		// !!! SD
					// partition
					return hoare_block_partition_simple(begin + 1, end - 1, mid, less);
				}
			};

			//const int IS_THRESH = 32;
			// Quicksort main loop. Implementation based on Tuned Quicksort (Elmasry, Katajainen, Stenmark)
			template<template<class, class> class Partitioner, typename SmallSorter, unsigned SMALL_SORT_THRESHOLD, typename iter, typename Compare>
			inline void new_qsort(iter begin, iter end, Compare less, SmallSorter& small_sorter) {
				if (end - begin < SMALL_SORT_THRESHOLD)
				{
					small_sorter(begin, end - begin);
					return;
				}
				const int depth_limit = 2 * ilogb((double)(end - begin)) + 3;
				iter stack[80];
				iter* s = stack;
				int depth_stack[40];
				int depth = 0;
				int* d_s_top = depth_stack;

				*s = begin;
				*(s + 1) = end;
				s += 2;
				*d_s_top = 0;
				++d_s_top;

				do {
					if (depth < depth_limit && end - begin > SMALL_SORT_THRESHOLD) {
						iter pivot = Partitioner<iter, Compare>::partition(begin, end, less);
						// Push large side to stack and continue on small side
						if (pivot - begin > end - pivot) {
							*s = begin;
							*(s + 1) = pivot;
							begin = pivot + 1;
						}
						else {
							*s = pivot + 1;
							*(s + 1) = end;
							end = pivot;
						}
						s += 2;
						depth++;
						*d_s_top = depth;
						++d_s_top;
					}
					else {
						if (end - begin > SMALL_SORT_THRESHOLD) // if recursion depth limit exceeded
							std::partial_sort(begin, end, end, less);
						//				std::sort(begin, end);
						else
							//				Insertionsort::insertion_sort(begin, end, less); // copy of std::__insertion_sort(GCC 4.7.2)
							//				std::sort(begin, end, less); // copy of std::__insertion_sort(GCC 4.7.2)
							//ins_sort1a(begin, end);
							small_sorter(begin, end - begin);
						//pop new subarray from stack
						s -= 2;
						begin = *s;
						end = *(s + 1);
						--d_s_top;
						depth = *d_s_top;
					}
				} while (s != stack);
			}

			template<typename T, typename SmallSorter, unsigned SMALL_SORT_THRESHOLD>
			void custom_quick_sort(T* start, T* end, SmallSorter& small_sorter)
			{
				new_qsort<Hoare_block_partition_simple, SmallSorter, SMALL_SORT_THRESHOLD>(start, end, LessFirstNotEqual<T>{}, small_sorter);
			}
#else
			template<typename T, typename SmallSorter, unsigned SMALL_SORT_THRESHOLD>
			void custom_quick_sort(T* start, T* end, SmallSorter& small_sorter)
			{
				std::sort(start, end, LessFirstNotEqual<T>{});
			}
#endif		
		}
	}
}