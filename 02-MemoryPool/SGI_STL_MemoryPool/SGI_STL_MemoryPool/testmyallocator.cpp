#include "myallocator.h"
#include<vector>
using namespace std;

int main()
{
	vector<int, myallcoator<int>> vec;

	for (int i = 1; i <= 100; i++) vec.push_back(i);

	for (auto i : vec) cout << i << " ";
	cout << endl;
	return 0;
}