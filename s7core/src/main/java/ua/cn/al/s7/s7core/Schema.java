/*
 * Copyright (C) 2021 Oleksiy Lukin alukin@gmail.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
package ua.cn.al.s7.s7core;

import com.google.common.graph.ElementOrder;
import com.google.common.graph.MutableNetwork;
import com.google.common.graph.NetworkBuilder;
import java.util.ArrayList;
import java.util.List;

/**
 *
 * @author Oleksiy Lukin alukin@gmail.com
 */
public class Schema extends Element {

    MutableNetwork<Element,Connection> elementGraph = NetworkBuilder.directed()
            .allowsParallelEdges(true)
            //.nodeOrder(ElementOrder???)
            .build();
    
    public boolean verify() {
        boolean res = true;
        //TODO: all unconnected inputs should be "grounded"
        // graph should be consistent
        // input type sshould be able to handle conneced output
        return res;
    }

    @Override
    public void tick() {
        for (Element e: elementGraph.nodes()){
            e.tick();
        }
    }

    public void insert(Element e) {
        elementGraph.addNode(e);
    }

    public void connect(Output out, Input in) {
        in.connect(out);
        Connection con = new Connection(out, in);
        elementGraph.addEdge(out.getOwnerElement(), in.getOwnerElement(), con);
    }

    public void runTicks(int nTicks) {
        //run ticks in graph iterator order
        for (int i = 0; i < nTicks; i++) {
            tick();
        }
    }
}
