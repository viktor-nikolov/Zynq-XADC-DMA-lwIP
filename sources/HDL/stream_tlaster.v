/*
This Verilog module is part of the XADC tutorial. It controls when the data from
the slave AXI-Stream interface starts to be sent to the master AXI-Stream interface.
It also controls how many data transfers are made and asserts the TLAST signal
on the last transfer.

Details are explained on GitHub: https://github.com/viktor-nikolov/Zynq-XADC-DMA-lwIP

BSD 2-Clause License:

Copyright (c) 2024 Viktor Nikolov

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
`timescale 1ns / 1ps

module stream_tlaster(
    input clk,          //AXI-Stream clock
    input start,
    input [24:0] count,
    
    //Master AXI-Stream signals
    output reg [15:0] m_axis_tdata,
    output reg m_axis_tvalid,
    output reg m_axis_tlast,
    input m_axis_tready,

    //Slave AXI-Stream signals
    input [15:0] s_axis_tdata,
    input s_axis_tvalid,
    output s_axis_tready
);

    // State definitions
    localparam IDLE = 1'b0,
               RUNNING = 1'b1;

    // State and internal signals
    reg state = IDLE, next_state;
    reg [24:0] valid_count;
    reg s_axis_tvalid_prev;

    // s_axis_tready is always 1
    assign s_axis_tready = 1;

    // Next state logic and outputs
    always @(posedge clk) begin
        case (state)
            IDLE: begin
                // Reset everything
                valid_count <= 0;
                s_axis_tvalid_prev <= 0;
                m_axis_tlast <= 0;
                m_axis_tvalid <= 0;

                // Transition to RUNNING when start is asserted
                if (start)
                    state <= RUNNING;
            end
            RUNNING: begin
                // Pass through data and valid signal
                m_axis_tdata <= s_axis_tdata;
                m_axis_tvalid <= s_axis_tvalid;

                // Check for transition from 0 to 1 in s_axis_tvalid
                if (!s_axis_tvalid_prev && s_axis_tvalid) begin
                    valid_count <= valid_count + 1;
                    // Check if the transition count reaches 'count'
                    if (valid_count == count-1) begin
                        m_axis_tlast <= 1;
                        state <= IDLE;
                    end else begin
                        m_axis_tlast <= 0;
                    end
                end else begin
                    m_axis_tlast <= 0;
                end

                // Update the previous valid signal state
                s_axis_tvalid_prev <= s_axis_tvalid;
            end
        endcase
    end

endmodule