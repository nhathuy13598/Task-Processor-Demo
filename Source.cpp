#include <memory>
#include <iostream>
#include <functional>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/post.hpp>
#include <future>
#include <chrono>

using namespace std::chrono;

boost::asio::thread_pool g_worker(1);

class Task {
private:
	std::promise<int> _value_promise;
	std::future<int> _value;
public:
	Task() : _value_promise{}, _value{ _value_promise.get_future() } {}
	void set_result(int value) {
		_value_promise.set_value(value);
	}

	int get_result() {
		try {
			return _value.wait_for(0s) == std::future_status::ready ? _value.get() : 0;
		}
		catch (const std::exception& e) {
			std::cout << e.what() << std::endl;
			return 0;
		}
	}
};

class IProcessor : public std::enable_shared_from_this<IProcessor> {
public:
	using FunctionHandler = std::function<void(std::shared_ptr<Task>)>;

protected:
	std::weak_ptr<Task> _task;

public:
	IProcessor(std::weak_ptr<Task> task) : _task(task) {}
	virtual void Execute() = 0;

protected:
	void AsyncExecute(FunctionHandler executor) {
		auto me = this->shared_from_this();
		boost::asio::post(g_worker, [me, executor]() {
			me->run_if_task_valid(executor);
		});
	}

	void run_if_task_valid(FunctionHandler functor) {
		auto task = _task.lock();
		if (!task) {
			return;
		}
		functor(task);
	}
};

class Local : public IProcessor {
private:
	int data = 5;

public:
	Local(std::weak_ptr<Task> task) : IProcessor(task) {}

	virtual void Execute() override {
		this->AsyncExecute(std::bind(&Local::Process, this, std::placeholders::_1));
	}

private:
	void Process(std::shared_ptr<Task> task) {
		std::cout << "In Process: " << data << std::endl;
		this->AsyncExecute(std::bind(&Local::Process2, this, std::placeholders::_1));
	}

	void Process2(std::shared_ptr<Task> task) {
		std::cout << "In Process2: " << ++data << std::endl;
		if (data == 10) {
			task->set_result(data);
			return;
		}
		this->AsyncExecute(std::bind(&Local::Process, this, std::placeholders::_1));
	}
};

int main() {
	std::shared_ptr<Task> task = std::make_shared<Task>();
	std::shared_ptr<IProcessor> local = std::make_shared<Local>(std::weak_ptr<Task>(task));
	local->Execute();
	std::cout << "Task result: " << task->get_result() << std::endl;
	g_worker.join();
	return 0;
}