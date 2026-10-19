#include "nonlinear_pendulum_cases.hpp"
#include <iostream>

int main() try {
    namespace p=s7tests::nonlinear_pendulum;
    s7::register_builtin_elements(s7::ElementRegistry::instance());
    const auto& ref=p::reference();
    const std::filesystem::path output(S7_SINGLE_PENDULUM_REPORT_DIR);
    std::filesystem::create_directories(output);
    std::ofstream summary(output/"summary.csv");
    if (!summary) throw std::runtime_error("Cannot write nonlinear summary");
    summary<<std::setprecision(17)<<"method,frequency_hz,duration_s,quality,max_angle_rad,rms_angle_rad,max_velocity_rad_s,max_relative_energy_error,final_relative_energy_growth,mean_period_s,max_period_error_s,max_crossing_drift_s,short_angle_rad,max_endpoint_residual,crossings\n";
    std::ofstream reference(output/"reference.txt");
    reference<<std::setprecision(20)<<"analytical_period_s="<<ref.period
             <<"\nrk4_dt_0.002_error="<<ref.rk4_coarse_error<<"\nrk4_dt_0.001_error="<<ref.rk4_fine_error<<'\n';
    std::cout<<std::setprecision(17)<<"Reference period="<<ref.period<<" s; RK4 errors="
             <<ref.rk4_coarse_error<<", "<<ref.rk4_fine_error<<std::endl;
    for (const auto method:p::methods) {
        double previous=0;
        for (const int frequency:p::frequencies) {
            const auto result=p::run(method,frequency,nullptr,output/(p::name(method)+"_"+std::to_string(frequency)+".csv"));
            summary<<method<<','<<frequency<<','<<p::duration<<','<<(result.quality_pass?"PASS":"FAIL_METHOD_LIMIT")<<','
                   <<result.max_angle<<','<<result.rms_angle()<<','<<result.max_velocity<<','<<result.max_energy<<','
                   <<result.final_energy_growth<<','<<result.mean_period<<','<<result.max_period_error<<','
                   <<result.max_crossing_drift<<','<<result.short_angle<<','<<result.max_endpoint_residual<<','<<result.crossings<<'\n';
            summary.flush();
            std::cout<<method<<' '<<frequency<<" Hz: quality="<<(result.quality_pass?"PASS":"FAIL (expected method limitation)")
                     <<" angle="<<result.max_angle<<" energy="<<result.max_energy<<" period error="<<result.max_period_error<<std::endl;
            if (previous) {
                const auto& ratio=p::budgets().at("short_time_error_ratio_for_10x_rate").as_object().at(method).as_array();
                if (previous/result.short_angle<ratio[0].to_number<double>() || previous/result.short_angle>ratio[1].to_number<double>()) {
                    throw std::runtime_error(p::name(method)+": short-time refinement order mismatch");
                }
            }
            previous=result.short_angle;
        }
    }
    return 0;
} catch (const std::exception& ex) { std::cerr<<ex.what()<<'\n'; return 1; }
