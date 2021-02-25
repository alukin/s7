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

import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import org.jgrapht.Graph;
import org.jgrapht.graph.DirectedMultigraph;
import org.jgrapht.graph.builder.GraphTypeBuilder;

/**
 *
 * @author Oleksiy Lukin alukin@gmail.com
 */
public class Schema extends Element {
 Graph<Element,Connection> elementGraph;
Element root;
 public Schema() {
    elementGraph = GraphTypeBuilder
            .directed()
            .vertexClass(Element.class)
            .edgeClass(Connection.class)
            .allowingMultipleEdges(true)
            .weighted(false)
            .allowingSelfLoops(false)
            .buildGraph();
    
     root = new Element() {
        @Override
        public void tick() {
        }
     };
     root.name="Root";
     //we need root to connect all "generators", e.g. constants
     elementGraph.addVertex(root);
    }
    

    
    public boolean verify() {
        boolean res = true;
        //TODO: all unconnected inputs should be "grounded"
        // graph should be consistent
        // input type sshould be able to handle conneced output
        return res;
    }

    @Override
    public void tick() {
        for (Element e: inCalcOrder()){
            e.tick();
        }
    }
    
    private Set<Element> inCalcOrder(){
        
        return elementGraph.vertexSet();
    }
    
    public void insert(Element e) {
       if(!elementGraph.containsVertex(e)){ 
        elementGraph.addVertex(e);
       }
       //we have to add generators as root's childs
       if(e.isGenerator()){
           Connection con = new Connection(root.getOut(0), e.getIn(0));
           elementGraph.addEdge(root, e, con);
       }
    }

    public void connect(Output out, Input in) {
        in.connect(out);
        insert(out.getOwnerElement());
        insert(in.getOwnerElement());
        Connection con = new Connection(out, in);        
        elementGraph.addEdge(out.getOwnerElement(), in.getOwnerElement(), con);
    }

    public void runTicks(int nTicks) {
        //run ticks in graph iterator order
        for (int i = 0; i < nTicks; i++) {
            tick();
        }
    }

    public String printOrder() {
        StringBuilder res=new StringBuilder();
        res.append("||->");
        for (Element e: inCalcOrder()){
            res=res.append("[").append(e.name).append("]->");           
        }
        res.append("||");
        return res.toString();
    }
}
