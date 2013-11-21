#pragma once



class Task
{
	public:
		Task();
		~Task();

		virtual int task() = 0;

		bool create();
		bool run();
		bool destroy();

	private:
		struct TaskImpl* m_implementation;
};