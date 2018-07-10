#ifndef RANGE_H
#define RANGE_H

template<typename T>
struct Range final
{
   const T begin_{};
   const T end_{};

   auto begin() { return begin_; }
   auto end() { return end_; }
};

template<typename T>
auto
range(const T begin, const T end)
{
   return Range<T>{begin, end};
}

template<typename T>
auto
range(T* const begin, T* const end)
{
   return Range<T*>{begin, end};
}

template<typename C>
auto
range(C& container)
{
   return range(container.begin(), container.end());
}

template<typename C>
auto
range(const C& container)
{
   return range(container.begin(), container.end());
}

template<typename T>
auto
range(T* const arr, const size_t n)
{
   return range(arr, arr + n);
}

template<typename T, size_t N>
auto
range(T (&arr)[N])
{
   return range(arr, N);
}

#endif // RANGE_H
