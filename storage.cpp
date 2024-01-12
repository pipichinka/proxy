#include "storage.hpp"


storage_t* storage; // for extern in storage.hpp


item_t::item_t(): started(false) ,compleated(false), pin_count(0){
    pthread_rwlock_init(&rw_lock, NULL);
}


item_t::~item_t(){
    pthread_rwlock_destroy(&rw_lock);
}

/*
 * Не используется (?)
 * */
void item_t::pin(){
    pthread_rwlock_wrlock(&rw_lock);
    ++pin_count;
    pthread_rwlock_unlock(&rw_lock);
}

/*
 * А это используется (?)
 * */
void item_t::un_pin(int fd){
    pthread_rwlock_wrlock(&rw_lock);
    if (pin_count > 0){
        --pin_count;
    }
    if (fd <= -1){
        pthread_rwlock_unlock(&rw_lock);
        return;
    }
    for (auto it = waiting_clients.begin(); it < waiting_clients.end(); ++it){
        if (it->get_fd() == fd){
            waiting_clients.erase(it);
            break;
        }
    }
    pthread_rwlock_unlock(&rw_lock);
}


void item_t::put_data(const std::string& s) noexcept{
    pthread_rwlock_wrlock(&rw_lock);
    data.append(s);
    size_t len = waiting_clients.size();
    for (size_t i = 0; i < len; ++i){
        waiting_clients[i].get_sel_con()->change_descriptor_mode(waiting_clients[i].get_fd(), WRITE);
    }
    waiting_clients.clear();
    pthread_rwlock_unlock(&rw_lock);
}


int item_t::get_data(std::string& dst, size_t offset, size_t limit, const wait_context_t& wait_context) noexcept{
    pthread_rwlock_wrlock(&rw_lock);
    if (data.length() == offset && compleated){
        pthread_rwlock_unlock(&rw_lock);
        return -1;
    }

    if (data.length() <= offset){
        waiting_clients.push_back(wait_context);
        wait_context.get_sel_con()->change_descriptor_mode(wait_context.get_fd(), 0);
        pthread_rwlock_unlock(&rw_lock);
        return 0;
    }

    dst = data.substr(offset, limit);    
    size_t ret = dst.length();
    pthread_rwlock_unlock(&rw_lock);
    return ret;
}


/*
 * completed
 * */
void item_t::set_complited(bool val) noexcept{
    pthread_rwlock_wrlock(&rw_lock);
    compleated = val;
    if (compleated){
        size_t len = waiting_clients.size();
        for (size_t i = 0; i < len; ++i){
            waiting_clients[i].get_sel_con()->change_descriptor_mode(waiting_clients[i].get_fd(), WRITE);
        }
        waiting_clients.clear();
    }
    pthread_rwlock_unlock(&rw_lock);
}


bool item_t::set_started(bool val) noexcept {
    pthread_rwlock_wrlock(&rw_lock);
    bool prev = started;
    started = val;
    pthread_rwlock_unlock(&rw_lock);
    return prev;
}



storage_t::storage_t() {
    pthread_mutex_init(&lock, NULL);
}


std::pair<std::string, std::shared_ptr<item_t>> storage_t::get_item(const std::string& key) noexcept{
    pthread_mutex_lock(&lock);
    auto it = hash_map.find(key);
    if (it == hash_map.end()){
        std::shared_ptr<item_t> item = std::make_shared<item_t>();
        auto result = hash_map.emplace(key, item);
        pthread_mutex_unlock(&lock);
        return *result.first;
    }
    pthread_mutex_unlock(&lock);
    return *it;
}


storage_t::~storage_t(){
    pthread_mutex_destroy(&lock);
}


void init_global_storage(){
    storage = new storage_t();
}


void storage_t::remove_item(const std::string& key) noexcept{
    hash_map.erase(key);
}